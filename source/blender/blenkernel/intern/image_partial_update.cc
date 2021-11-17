/**
 * \file image_gpu_partial_update.cc
 *
 * To reduce the overhead of image processing this file contains a mechanism to detect areas of the
 * image that are changed. These areas are organized in chunks. Changes that happen over time are
 * organized in changesets.
 *
 * A common usecase is to update GPUTexture for drawing where only that part is uploaded that only
 * changed.
 *
 * Usage:
 *
 * ```
 * Image *image = ...;
 * ImBuf *image_buffer = ...;
 *
 * // partial_update_user should be kept for the whole session where the changes needs to be
 * // tracked. Keep this instance alive as long as you need to track image changes.
 *
 * PartialUpdateUser *partial_update_user = BKE_image_partial_update_create(image);
 *
 * ...
 *
 * switch (BKE_image_partial_update_collect_changes(image, image_buffer))
 * {
 * case PARTIAL_UPDATE_NEED_FULL_UPDATE:
 *  // Unable to do partial updates. Perform a full update.
 *  break;
 * case PARTIAL_UPDATE_CHANGES_AVAILABLE:
 *  PartialUpdateRegion change;
 *  while (BKE_image_partial_update_get_next_change(partial_update_user, &change) ==
 *         PARTIAL_UPDATE_ITER_CHANGE_AVAILABLE){
 *  // Do something with the change.
 *  }
 *  case PARTIAL_UPDATE_NO_CHANGES:
 *    break;
 * }
 *
 * ...
 *
 * // Free partial_update_user.
 * BKE_image_partial_update_free(partial_update_user);
 *
 * ```
 */

#include "BKE_image.h"

#include "DNA_image_types.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "BLI_vector.hh"

namespace blender::bke::image::partial_update {

struct PartialUpdateUserImpl;
struct PartialUpdateRegisterImpl;

/**
 * Wrap PartialUpdateUserImpl to its C-struct (PartialUpdateUser).
 */
static struct PartialUpdateUser *wrap(PartialUpdateUserImpl *user)
{
  return static_cast<struct PartialUpdateUser *>(static_cast<void *>(user));
}

/**
 * Unwrap the PartialUpdateUser C-struct to its CPP counterpart (PartialUpdateUserImpl).
 */
static PartialUpdateUserImpl *unwrap(struct PartialUpdateUser *user)
{
  return static_cast<PartialUpdateUserImpl *>(static_cast<void *>(user));
}

/**
 * Wrap PartialUpdateRegisterImpl to its C-struct (PartialUpdateRegister).
 */
static struct PartialUpdateRegister *wrap(PartialUpdateRegisterImpl *partial_update_register)
{
  return static_cast<struct PartialUpdateRegister *>(static_cast<void *>(partial_update_register));
}

/**
 * Unwrap the PartialUpdateRegister C-struct to its CPP counterpart (PartialUpdateRegisterImpl).
 */
static PartialUpdateRegisterImpl *unwrap(struct PartialUpdateRegister *partial_update_register)
{
  return static_cast<PartialUpdateRegisterImpl *>(static_cast<void *>(partial_update_register));
}

using TileNumber = int32_t;
using ChangesetID = int64_t;
constexpr ChangesetID UnknownChangesetID = -1;

struct PartialUpdateUserImpl {
  /** \brief last changeset id that was seen by this user. */
  ChangesetID last_changeset_id = UnknownChangesetID;

  /** \brief regions that have been updated. */
  Vector<PartialUpdateRegion> updated_regions;

#ifdef NDEBUG
  /** \brief reference to image to validate correct API usage. */
  void *debug_image_;
#endif

  /**
   * \brief Clear the list of updated regions.
   *
   * Updated regions should be cleared at the start of #BKE_image_partial_update_collect_changes so
   * the
   */
  void clear_updated_regions()
  {
    updated_regions.clear();
  }
};

/**
 * \brief Dirty chunks of an ImageTile.
 *
 * Internally dirty tiles are grouped together in change sets to make sure that the correct
 * answer can be built for different users reducing the amount of merges.
 */
// TODO(jbakker): TileChangeset is per UDIM tile. There should be an
struct TileChangeset {
 private:
  /** \brief Dirty flag for each chunk. */
  std::vector<bool> chunk_dirty_flags_;
  /** \brief are there dirty/ */
  bool has_dirty_chunks_ = false;

 public:
  /** \brief Number of chunks along the x-axis. */
  int chunk_x_len_;
  /** \brief Number of chunks along the y-axis. */
  int chunk_y_len_;

  bool has_dirty_chunks() const
  {
    return has_dirty_chunks_;
  }

  void init_chunks(int chunk_x_len, int chunk_y_len)
  {
    chunk_x_len_ = chunk_x_len;
    chunk_y_len_ = chunk_y_len;
    const int chunk_len = chunk_x_len * chunk_y_len;
    const int previous_chunk_len = chunk_dirty_flags_.size();

    chunk_dirty_flags_.resize(chunk_len);
    /* Fast exit. When the changeset was already empty no need to re-init the chunk_validity. */
    if (!has_dirty_chunks()) {
      return;
    }
    for (int index = 0; index < min_ii(chunk_len, previous_chunk_len); index++) {
      chunk_dirty_flags_[index] = false;
    }
    has_dirty_chunks_ = false;
  }

  void reset()
  {
    init_chunks(chunk_x_len_, chunk_y_len_);
  }

  void mark_chunks_dirty(int start_x_chunk, int start_y_chunk, int end_x_chunk, int end_y_chunk)
  {
    for (int chunk_y = start_y_chunk; chunk_y <= end_y_chunk; chunk_y++) {
      for (int chunk_x = start_x_chunk; chunk_x <= end_x_chunk; chunk_x++) {
        int chunk_index = chunk_y * chunk_x_len_ + chunk_x;
        chunk_dirty_flags_[chunk_index] = true;
      }
    }
    has_dirty_chunks_ = true;
  }

  /** \brief Merge the given changeset into the receiver. */
  void merge(const TileChangeset &other)
  {
    BLI_assert(chunk_x_len_ == other.chunk_x_len_);
    BLI_assert(chunk_y_len_ == other.chunk_y_len_);
    const int chunk_len = chunk_x_len_ * chunk_y_len_;

    for (int chunk_index = 0; chunk_index < chunk_len; chunk_index++) {
      chunk_dirty_flags_[chunk_index] = chunk_dirty_flags_[chunk_index] |
                                        other.chunk_dirty_flags_[chunk_index];
    }
    has_dirty_chunks_ |= other.has_dirty_chunks_;
  }

  /** \brief has a chunk changed inside this changeset. */
  bool is_chunk_dirty(int chunk_x, int chunk_y) const
  {
    const int chunk_index = chunk_y * chunk_x_len_ + chunk_x;
    return chunk_dirty_flags_[chunk_index];
  }
};

struct Changeset {
  TileChangeset tile_changeset;
};

/**
 * \brief Partial update changes stored inside the image runtime.
 *
 * The PartialUpdateRegisterImpl will keep track of changes over time. Changes are groups inside
 * TileChangesets.
 */
struct PartialUpdateRegisterImpl {
  /* Changes are tracked in chunks. */
  static constexpr int CHUNK_SIZE = 256;

  /** \brief changeset id of the first changeset kept in #history. */
  ChangesetID first_changeset_id;
  /** \brief changeset id of the top changeset kept in #history. */
  ChangesetID last_changeset_id;

  /** \brief history of changesets. */
  Vector<Changeset> history;
  /** \brief The current changeset. New changes will be added to this changeset only. */
  Changeset current_changeset;

  int image_width;
  int image_height;

  void update_resolution(ImBuf *image_buffer)
  {
    if (image_width != image_buffer->x || image_height != image_buffer->y) {
      image_width = image_buffer->x;
      image_height = image_buffer->y;

      int chunk_x_len = image_width / CHUNK_SIZE;
      int chunk_y_len = image_height / CHUNK_SIZE;
      current_changeset.tile_changeset.init_chunks(chunk_x_len, chunk_y_len);

      /* Only perform a full update when the cache contains data. */
      if (current_changeset.tile_changeset.has_dirty_chunks() || !history.is_empty()) {
        mark_full_update();
      }
    }
  }

  void mark_full_update()
  {
    history.clear();
    last_changeset_id++;
    current_changeset.tile_changeset.reset();
    first_changeset_id = last_changeset_id;
  }

  /**
   * \brief get the chunk number for the give pixel coordinate.
   *
   * As chunks are squares the this member can be used for both x and y axis.
   */
  static int chunk_number_for_pixel(int pixel_offset)
  {
    int chunk_offset = pixel_offset / CHUNK_SIZE;
    if (pixel_offset < 0) {
      chunk_offset -= 1;
    }
    return chunk_offset;
  }

  void mark_region(rcti *updated_region)
  {
    int start_x_chunk = chunk_number_for_pixel(updated_region->xmin);
    int end_x_chunk = chunk_number_for_pixel(updated_region->xmax - 1);
    int start_y_chunk = chunk_number_for_pixel(updated_region->ymin);
    int end_y_chunk = chunk_number_for_pixel(updated_region->ymax - 1);

    /* Clamp tiles to tiles in image. */
    start_x_chunk = max_ii(0, start_x_chunk);
    start_y_chunk = max_ii(0, start_y_chunk);
    end_x_chunk = min_ii(current_changeset.tile_changeset.chunk_x_len_ - 1, end_x_chunk);
    end_y_chunk = min_ii(current_changeset.tile_changeset.chunk_y_len_ - 1, end_y_chunk);

    /* Early exit when no tiles need to be updated. */
    if (start_x_chunk >= current_changeset.tile_changeset.chunk_x_len_) {
      return;
    }
    if (start_y_chunk >= current_changeset.tile_changeset.chunk_y_len_) {
      return;
    }
    if (end_x_chunk < 0) {
      return;
    }
    if (end_y_chunk < 0) {
      return;
    }

    current_changeset.tile_changeset.mark_chunks_dirty(
        start_x_chunk, start_y_chunk, end_x_chunk, end_y_chunk);
  }

  void ensure_empty_changeset()
  {
    if (!current_changeset.tile_changeset.has_dirty_chunks()) {
      /* No need to create a new changeset when previous changeset does not contain any dirty
       * tiles. */
      return;
    }
    commit_current_changeset();
  }

  /** Move the current changeset to the history and resets the current changeset. */
  void commit_current_changeset()
  {
    history.append_as(std::move(current_changeset));
    current_changeset.tile_changeset.reset();
    last_changeset_id++;
  }

  /**
   * /brief Check if data is available to construct the update tiles for the given
   * changeset_id.
   *
   * The update tiles can be created when changeset id is between
   */
  bool can_construct(ChangesetID changeset_id)
  {
    return changeset_id >= first_changeset_id;
  }

  /**
   * \brief collect all historic changes since a given changeset.
   */
  // TODO(jbakker): add tile_number as parameter.
  std::unique_ptr<TileChangeset> changed_tile_chunks_since(const TileNumber UNUSED(tile_number), const ChangesetID from_changeset)
  {
    std::unique_ptr<TileChangeset> changed_tiles = std::make_unique<TileChangeset>();
    int chunk_x_len = image_width / CHUNK_SIZE;
    int chunk_y_len = image_height / CHUNK_SIZE;
    changed_tiles->init_chunks(chunk_x_len, chunk_y_len);

    for (int index = from_changeset - first_changeset_id; index < history.size(); index++) {
      changed_tiles->merge(history[index].tile_changeset);
    }
    return changed_tiles;
  }
};

}  // namespace blender::bke::image::partial_update

extern "C" {

using namespace blender::bke::image::partial_update;

static struct PartialUpdateRegister *image_partial_update_register_ensure(Image *image)
{
  if (image->runtime.partial_update_register == nullptr) {
    PartialUpdateRegisterImpl *partial_update_register = OBJECT_GUARDED_NEW(
        PartialUpdateRegisterImpl);
    image->runtime.partial_update_register = wrap(partial_update_register);
  }
  return image->runtime.partial_update_register;
}

// TODO(jbakker): cleanup parameter.
struct PartialUpdateUser *BKE_image_partial_update_create(struct Image *image)
{
  PartialUpdateUserImpl *user_impl = OBJECT_GUARDED_NEW(PartialUpdateUserImpl);

#ifdef NDEBUG
  user_impl->debug_image_ = image;
#else
  UNUSED_VARS(image);
#endif

  return wrap(user_impl);
}

void BKE_image_partial_update_free(PartialUpdateUser *user)
{
  PartialUpdateUserImpl *user_impl = unwrap(user);
  OBJECT_GUARDED_DELETE(user_impl, PartialUpdateUserImpl);
}

ePartialUpdateCollectResult BKE_image_partial_update_collect_changes(Image *image,
                                                                     PartialUpdateUser *user)
{
  PartialUpdateUserImpl *user_impl = unwrap(user);
#ifdef NDEBUG
  BLI_assert(image == user_impl->debug_image_);
#endif

  user_impl->clear_updated_regions();

  PartialUpdateRegisterImpl *partial_updater = unwrap(image_partial_update_register_ensure(image));
  partial_updater->ensure_empty_changeset();

  if (!partial_updater->can_construct(user_impl->last_changeset_id)) {
    user_impl->last_changeset_id = partial_updater->last_changeset_id;
    return PARTIAL_UPDATE_NEED_FULL_UPDATE;
  }

  /* Check if there are changes since last invocation for the user. */
  if (user_impl->last_changeset_id == partial_updater->last_changeset_id) {
    return PARTIAL_UPDATE_NO_CHANGES;
  }

  /* Collect changed tiles. */
  LISTBASE_FOREACH (ImageTile *, tile, &image->tiles) {
    std::unique_ptr<TileChangeset> changed_chunks = partial_updater->changed_tile_chunks_since(tile->tile_number,
        user_impl->last_changeset_id);
    /* Check if chunks of this tile are dirty. */
    if (!changed_chunks->has_dirty_chunks()) {
      continue;
    }

    /* Convert tiles in the changeset to rectangles that are dirty. */
    for (int chunk_y = 0; chunk_y < changed_chunks->chunk_y_len_; chunk_y++) {
      for (int chunk_x = 0; chunk_x < changed_chunks->chunk_x_len_; chunk_x++) {
        if (!changed_chunks->is_chunk_dirty(chunk_x, chunk_y)) {
          continue;
        }

        PartialUpdateRegion region;
        region.tile_number = tile->tile_number;
        BLI_rcti_init(&region.region,
                      chunk_x * PartialUpdateRegisterImpl::CHUNK_SIZE,
                      (chunk_x + 1) * PartialUpdateRegisterImpl::CHUNK_SIZE,
                      chunk_y * PartialUpdateRegisterImpl::CHUNK_SIZE,
                      (chunk_y + 1) * PartialUpdateRegisterImpl::CHUNK_SIZE);
        user_impl->updated_regions.append_as(region);
      }
    }
  }

  user_impl->last_changeset_id = partial_updater->last_changeset_id;
  return PARTIAL_UPDATE_CHANGES_AVAILABLE;
}

ePartialUpdateIterResult BKE_image_partial_update_get_next_change(PartialUpdateUser *user,
                                                                  PartialUpdateRegion *r_region)
{
  PartialUpdateUserImpl *user_impl = unwrap(user);
  if (user_impl->updated_regions.is_empty()) {
    return PARTIAL_UPDATE_ITER_FINISHED;
  }
  PartialUpdateRegion region = user_impl->updated_regions.pop_last();
  *r_region = region;
  return PARTIAL_UPDATE_ITER_CHANGE_AVAILABLE;
}

/* --- Image side --- */

void BKE_image_partial_update_register_free(Image *image)
{
  PartialUpdateRegisterImpl *partial_update_register = unwrap(
      image->runtime.partial_update_register);
  if (partial_update_register) {
    OBJECT_GUARDED_DELETE(partial_update_register, PartialUpdateRegisterImpl);
  }
  image->runtime.partial_update_register = nullptr;
}

void BKE_image_partial_update_mark_region(Image *image, ImBuf *image_buffer, rcti *updated_region)
{
  PartialUpdateRegisterImpl *partial_updater = unwrap(image_partial_update_register_ensure(image));
  partial_updater->update_resolution(image_buffer);
  partial_updater->mark_region(updated_region);
}

void BKE_image_partial_update_mark_full_update(Image *image)
{
  PartialUpdateRegisterImpl *partial_updater = unwrap(image_partial_update_register_ensure(image));
  partial_updater->mark_full_update();
}
}
