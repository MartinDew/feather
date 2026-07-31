#include "variant_array.h"
#include "cow_vector.h"
#include "variant.h"
#include <stdexcept>

namespace feather {

// Implementation class wraps CowVector<Variant>
class VariantArray::Buffer {
public:
	CowVector<Variant> data;

	Buffer() = default;
	explicit Buffer(size_t count) : data(count) {}
	Buffer(size_t count, const Variant& value) : data(count, value) {}
	Buffer(std::initializer_list<Variant> ilist) : data(ilist) {}

	template <typename InputIt>
	Buffer(InputIt first, InputIt last) : data(first, last) {}

	size_t use_count() const { return data.use_count(); }
};

void VariantArray::ensure_unique() {
	if (!_buffer || _buffer->use_count() > 1) {
		_buffer = std::make_unique<Buffer>(*_buffer);
	}
}

// Constructors
VariantArray::VariantArray() : _buffer(std::make_unique<Buffer>()) {
}

VariantArray::VariantArray(size_type count) : _buffer(std::make_unique<Buffer>(count)) {
}

VariantArray::VariantArray(size_type count, const Variant& value) : _buffer(std::make_unique<Buffer>(count, value)) {
}

VariantArray::VariantArray(std::initializer_list<Variant> ilist) : _buffer(std::make_unique<Buffer>(ilist)) {
}

// Explicit instantiations for common iterator types
template VariantArray::VariantArray(Variant*, Variant*);
template VariantArray::VariantArray(const Variant*, const Variant*);

// Copy and move
VariantArray::VariantArray(const VariantArray& other) : _buffer(std::make_unique<Buffer>(*other._buffer)) {
}

VariantArray::VariantArray(VariantArray&& other) noexcept : _buffer(std::move(other._buffer)) {
}

VariantArray& VariantArray::operator=(const VariantArray& other) {
	if (this != &other) {
		_buffer = std::make_unique<Buffer>(*other._buffer);
	}
	return *this;
}

VariantArray& VariantArray::operator=(VariantArray&& other) noexcept {
	if (this != &other) {
		_buffer = std::move(other._buffer);
	}
	return *this;
}

VariantArray& VariantArray::operator=(std::initializer_list<Variant> ilist) {
	_buffer = std::make_unique<Buffer>(ilist);
	return *this;
}

VariantArray::~VariantArray() = default;

// Element access
Variant& VariantArray::at(size_type pos) {
	ensure_unique();
	return _buffer->data.at(pos);
}

const Variant& VariantArray::at(size_type pos) const {
	return _buffer->data.at(pos);
}

Variant& VariantArray::operator[](size_type pos) {
	ensure_unique();
	return _buffer->data[pos];
}

const Variant& VariantArray::operator[](size_type pos) const {
	return _buffer->data[pos];
}

Variant& VariantArray::front() {
	ensure_unique();
	return _buffer->data.front();
}

const Variant& VariantArray::front() const {
	return _buffer->data.front();
}

Variant& VariantArray::back() {
	ensure_unique();
	return _buffer->data.back();
}

const Variant& VariantArray::back() const {
	return _buffer->data.back();
}

// Iterators
VariantArray::iterator VariantArray::begin() {
	ensure_unique();
	return iterator(&(*_buffer->data.begin()));
}

VariantArray::const_iterator VariantArray::begin() const noexcept {
	return const_iterator(&(*_buffer->data.begin()));
}

VariantArray::const_iterator VariantArray::cbegin() const noexcept {
	return const_iterator(&(*_buffer->data.cbegin()));
}

VariantArray::iterator VariantArray::end() {
	ensure_unique();
	return iterator(&(*_buffer->data.begin()) + _buffer->data.size());
}

VariantArray::const_iterator VariantArray::end() const noexcept {
	return const_iterator(&(*_buffer->data.begin()) + _buffer->data.size());
}

VariantArray::const_iterator VariantArray::cend() const noexcept {
	return const_iterator(&(*_buffer->data.cbegin()) + _buffer->data.size());
}

// Capacity
bool VariantArray::empty() const noexcept {
	return _buffer->data.empty();
}

VariantArray::size_type VariantArray::size() const noexcept {
	return _buffer->data.size();
}

VariantArray::size_type VariantArray::capacity() const noexcept {
	return _buffer->data.capacity();
}

void VariantArray::reserve(size_type new_cap) {
	ensure_unique();
	_buffer->data.reserve(new_cap);
}

void VariantArray::shrink_to_fit() {
	ensure_unique();
	_buffer->data.shrink_to_fit();
}

// Modifiers
void VariantArray::clear() noexcept {
	ensure_unique();
	_buffer->data.clear();
}

void VariantArray::push_back(const Variant& value) {
	ensure_unique();
	_buffer->data.push_back(value);
}

void VariantArray::push_back(Variant&& value) {
	ensure_unique();
	_buffer->data.push_back(std::move(value));
}

template <typename... Args>
Variant& VariantArray::emplace_back(Args&&... args) {
	ensure_unique();
	return _buffer->data.emplace_back(std::forward<Args>(args)...);
}

// Explicit instantiation for common cases
template Variant& VariantArray::emplace_back();
template Variant& VariantArray::emplace_back(Variant&&);
template Variant& VariantArray::emplace_back(const Variant&);

void VariantArray::pop_back() {
	ensure_unique();
	_buffer->data.pop_back();
}

void VariantArray::resize(size_type count) {
	ensure_unique();
	_buffer->data.resize(count);
}

void VariantArray::resize(size_type count, const Variant& value) {
	ensure_unique();
	_buffer->data.resize(count, value);
}

// COW-specific
bool VariantArray::is_shared() const noexcept {
	return _buffer->data.use_count() > 1;
}

size_t VariantArray::use_count() const noexcept {
	return _buffer->use_count();
}

// Comparison
bool VariantArray::operator==(const VariantArray& other) const {
	if (_buffer == other._buffer)
		return true;
	return _buffer->data == other._buffer->data;
}

bool VariantArray::operator!=(const VariantArray& other) const {
	return !(*this == other);
}

// Iterator implementations
VariantArray::iterator::iterator(Variant* ptr) : ptr_(ptr) {
}

Variant& VariantArray::iterator::operator*() const {
	return *ptr_;
}
Variant* VariantArray::iterator::operator->() const {
	return ptr_;
}

VariantArray::iterator& VariantArray::iterator::operator++() {
	++ptr_;
	return *this;
}

VariantArray::iterator VariantArray::iterator::operator++(int) {
	iterator tmp = *this;
	++ptr_;
	return tmp;
}

VariantArray::iterator& VariantArray::iterator::operator--() {
	--ptr_;
	return *this;
}

VariantArray::iterator VariantArray::iterator::operator--(int) {
	iterator tmp = *this;
	--ptr_;
	return tmp;
}

VariantArray::iterator& VariantArray::iterator::operator+=(difference_type n) {
	ptr_ += n;
	return *this;
}

VariantArray::iterator& VariantArray::iterator::operator-=(difference_type n) {
	ptr_ -= n;
	return *this;
}

VariantArray::iterator VariantArray::iterator::operator+(difference_type n) const {
	return iterator(ptr_ + n);
}

VariantArray::iterator VariantArray::iterator::operator-(difference_type n) const {
	return iterator(ptr_ - n);
}

ptrdiff_t VariantArray::iterator::operator-(const iterator& other) const {
	return ptr_ - other.ptr_;
}

Variant& VariantArray::iterator::operator[](difference_type n) const {
	return ptr_[n];
}

bool VariantArray::iterator::operator==(const iterator& other) const {
	return ptr_ == other.ptr_;
}

bool VariantArray::iterator::operator!=(const iterator& other) const {
	return ptr_ != other.ptr_;
}

bool VariantArray::iterator::operator<(const iterator& other) const {
	return ptr_ < other.ptr_;
}

bool VariantArray::iterator::operator<=(const iterator& other) const {
	return ptr_ <= other.ptr_;
}

bool VariantArray::iterator::operator>(const iterator& other) const {
	return ptr_ > other.ptr_;
}

bool VariantArray::iterator::operator>=(const iterator& other) const {
	return ptr_ >= other.ptr_;
}

// const_iterator implementations
VariantArray::const_iterator::const_iterator(const Variant* ptr) : ptr_(ptr) {
}

VariantArray::const_iterator::const_iterator(const iterator& it) : ptr_(&(*it)) {
}

const Variant& VariantArray::const_iterator::operator*() const {
	return *ptr_;
}
const Variant* VariantArray::const_iterator::operator->() const {
	return ptr_;
}

VariantArray::const_iterator& VariantArray::const_iterator::operator++() {
	++ptr_;
	return *this;
}

VariantArray::const_iterator VariantArray::const_iterator::operator++(int) {
	const_iterator tmp = *this;
	++ptr_;
	return tmp;
}

VariantArray::const_iterator& VariantArray::const_iterator::operator--() {
	--ptr_;
	return *this;
}

VariantArray::const_iterator VariantArray::const_iterator::operator--(int) {
	const_iterator tmp = *this;
	--ptr_;
	return tmp;
}

VariantArray::const_iterator& VariantArray::const_iterator::operator+=(difference_type n) {
	ptr_ += n;
	return *this;
}

VariantArray::const_iterator& VariantArray::const_iterator::operator-=(difference_type n) {
	ptr_ -= n;
	return *this;
}

VariantArray::const_iterator VariantArray::const_iterator::operator+(difference_type n) const {
	return const_iterator(ptr_ + n);
}

VariantArray::const_iterator VariantArray::const_iterator::operator-(difference_type n) const {
	return const_iterator(ptr_ - n);
}

ptrdiff_t VariantArray::const_iterator::operator-(const const_iterator& other) const {
	return ptr_ - other.ptr_;
}

const Variant& VariantArray::const_iterator::operator[](difference_type n) const {
	return ptr_[n];
}

bool VariantArray::const_iterator::operator==(const const_iterator& other) const {
	return ptr_ == other.ptr_;
}

bool VariantArray::const_iterator::operator!=(const const_iterator& other) const {
	return ptr_ != other.ptr_;
}

bool VariantArray::const_iterator::operator<(const const_iterator& other) const {
	return ptr_ < other.ptr_;
}

bool VariantArray::const_iterator::operator<=(const const_iterator& other) const {
	return ptr_ <= other.ptr_;
}

bool VariantArray::const_iterator::operator>(const const_iterator& other) const {
	return ptr_ > other.ptr_;
}

bool VariantArray::const_iterator::operator>=(const const_iterator& other) const {
	return ptr_ >= other.ptr_;
}

} // namespace feather