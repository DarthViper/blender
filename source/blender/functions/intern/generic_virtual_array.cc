/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "FN_generic_virtual_array.hh"

namespace blender::fn {

/* --------------------------------------------------------------------
 * GVArray.
 */

void GVArray::materialize_to_uninitialized(const IndexMask mask, void *dst) const
{
  for (const int64_t i : mask) {
    void *elem_dst = POINTER_OFFSET(dst, type_->size() * i);
    this->get_to_uninitialized(i, elem_dst);
  }
}

void GVArray::get_impl(const int64_t index, void *r_value) const
{
  type_->destruct(r_value);
  this->get_to_uninitialized_impl(index, r_value);
}

bool GVArray::is_span_impl() const
{
  return false;
}

GSpan GVArray::get_span_impl() const
{
  BLI_assert(false);
  return GSpan(*type_);
}

bool GVArray::is_single_impl() const
{
  return false;
}

void GVArray::get_single_impl(void *UNUSED(r_value)) const
{
  BLI_assert(false);
}

/* --------------------------------------------------------------------
 * GVArray_For_GSpan.
 */

void GVArray_For_GSpan::get_impl(const int64_t index, void *r_value) const
{
  type_->copy_to_initialized(POINTER_OFFSET(data_, element_size_ * index), r_value);
}

void GVArray_For_GSpan::get_to_uninitialized_impl(const int64_t index, void *r_value) const
{
  type_->copy_to_uninitialized(POINTER_OFFSET(data_, element_size_ * index), r_value);
}

bool GVArray_For_GSpan::is_span_impl() const
{
  return true;
}

GSpan GVArray_For_GSpan::get_span_impl() const
{
  return GSpan(*type_, data_, size_);
}

/* --------------------------------------------------------------------
 * GVMutableArray_For_GMutableSpan.
 */

void GVMutableArray_For_GMutableSpan::get_impl(const int64_t index, void *r_value) const
{
  type_->copy_to_initialized(POINTER_OFFSET(data_, element_size_ * index), r_value);
}

void GVMutableArray_For_GMutableSpan::get_to_uninitialized_impl(const int64_t index,
                                                                void *r_value) const
{
  type_->copy_to_uninitialized(POINTER_OFFSET(data_, element_size_ * index), r_value);
}

void GVMutableArray_For_GMutableSpan::set_by_copy_impl(const int64_t index, const void *value)
{
  type_->copy_to_initialized(value, POINTER_OFFSET(data_, element_size_ * index));
}

void GVMutableArray_For_GMutableSpan::set_by_move_impl(const int64_t index, void *value)
{
  type_->move_to_initialized(value, POINTER_OFFSET(data_, element_size_ * index));
}

void GVMutableArray_For_GMutableSpan::set_by_relocate_impl(const int64_t index, void *value)
{
  type_->relocate_to_initialized(value, POINTER_OFFSET(data_, element_size_ * index));
}

bool GVMutableArray_For_GMutableSpan::is_span_impl() const
{
  return true;
}

GSpan GVMutableArray_For_GMutableSpan::get_span_impl() const
{
  return GSpan(*type_, data_, size_);
}

/* --------------------------------------------------------------------
 * GVArray_For_SingleValueRef.
 */

void GVArray_For_SingleValueRef::get_impl(const int64_t UNUSED(index), void *r_value) const
{
  type_->copy_to_initialized(value_, r_value);
}

void GVArray_For_SingleValueRef::get_to_uninitialized_impl(const int64_t UNUSED(index),
                                                           void *r_value) const
{
  type_->copy_to_uninitialized(value_, r_value);
}

bool GVArray_For_SingleValueRef::is_span_impl() const
{
  return size_ == 1;
}

GSpan GVArray_For_SingleValueRef::get_span_impl() const
{
  return GSpan{*type_, value_, 1};
}

bool GVArray_For_SingleValueRef::is_single_impl() const
{
  return true;
}

void GVArray_For_SingleValueRef::get_single_impl(void *r_value) const
{
  type_->copy_to_initialized(value_, r_value);
}

/* --------------------------------------------------------------------
 * GVArray_For_SingleValue.
 */

GVArray_For_SingleValue::GVArray_For_SingleValue(const CPPType &type,
                                                 const int64_t size,
                                                 const void *value)
    : GVArray_For_SingleValueRef(type, size)
{
  value_ = MEM_mallocN_aligned(type.size(), type.alignment(), __func__);
  type.copy_to_uninitialized(value, (void *)value_);
}

GVArray_For_SingleValue::~GVArray_For_SingleValue()
{
  type_->destruct((void *)value_);
  MEM_freeN((void *)value_);
}

/* --------------------------------------------------------------------
 * GVArray_As_GSpan.
 */

GVArray_As_GSpan::GVArray_As_GSpan(const GVArray &varray)
    : GVArray_For_GSpan(varray.type(), varray.size()), varray_(varray)
{
  if (varray_.is_span()) {
    this->set_span_start(varray_.get_span().data());
  }
  else {
    owned_data_ = MEM_mallocN_aligned(type_->size() * size_, type_->alignment(), __func__);
    varray_.materialize_to_uninitialized(IndexRange(size_), owned_data_);
    this->set_span_start(owned_data_);
  }
}

GVArray_As_GSpan::~GVArray_As_GSpan()
{
  if (owned_data_ != nullptr) {
    type_->destruct_n(owned_data_, size_);
    MEM_freeN(owned_data_);
  }
}

GSpan GVArray_As_GSpan::as_span() const
{
  return this->get_span();
}

GVArray_As_GSpan::operator GSpan() const
{
  return this->get_span();
}

/* --------------------------------------------------------------------
 * GVMutableArray_As_GMutableSpan.
 */

GVMutableArray_As_GMutableSpan::GVMutableArray_As_GMutableSpan(GVMutableArray &varray)
    : GVMutableArray_For_GMutableSpan(varray.type(), varray.size()), varray_(varray)
{
  if (varray_.is_span()) {
    this->set_span_start(varray_.get_span().data());
  }
  else {
    owned_data_ = MEM_mallocN_aligned(type_->size() * size_, type_->alignment(), __func__);
    varray_.materialize_to_uninitialized(IndexRange(size_), owned_data_);
    this->set_span_start(owned_data_);
  }
}

GVMutableArray_As_GMutableSpan::~GVMutableArray_As_GMutableSpan()
{
  if (show_not_applied_warning_) {
    if (!apply_has_been_called_) {
      std::cout << "Warning: Call `apply()` to make sure that changes persist in all cases.\n";
    }
  }
}

void GVMutableArray_As_GMutableSpan::apply()
{
  apply_has_been_called_ = true;
  if (data_ != owned_data_) {
    return;
  }
  for (int64_t i : IndexRange(size_)) {
    varray_.set_by_copy(i, POINTER_OFFSET(owned_data_, element_size_ * i));
  }
}

void GVMutableArray_As_GMutableSpan::disable_not_applied_warning()
{
  show_not_applied_warning_ = false;
}

GMutableSpan GVMutableArray_As_GMutableSpan::as_span()
{
  return this->get_span();
}

GVMutableArray_As_GMutableSpan::operator GMutableSpan()
{
  return this->get_span();
}

}  // namespace blender::fn
