#pragma once

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <span>
#include <stdexcept>

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <malloc.h>
#define aligned_alloc(align, size) _aligned_malloc(size, align)
#define aligned_free _aligned_free
#elifdef __linux__
#include <malloc.h>
#define aligned_free free
#else
#define aligned_free free
#endif

namespace feather {

template <class T>
class CowVector {
private:
	struct buffer {
		T* data;
		size_t size;
		size_t capacity;
		mutable size_t ref_count;

		buffer(size_t cap = 0)
				: data(cap > 0 ? static_cast<T*>(aligned_alloc(alignof(T), sizeof(T) * cap)) : nullptr)
				, size(0)
				, capacity(cap)
				, ref_count(1) {}

		~buffer() {
			if (data) {
				// Destroy all constructed elements
				for (size_t i = 0; i < size; ++i) {
					data[i].~T();
				}
				aligned_free(data);
			}
		}

		// Non-copyable, non-movable
		buffer(const buffer&) = delete;
		buffer& operator=(const buffer&) = delete;
		buffer(buffer&&) = delete;
		buffer& operator=(buffer&&) = delete;
	};

	mutable std::shared_ptr<buffer> buf_;

	// Ensure we have a unique copy of the buffer
	void ensure_unique() {
		if (!buf_ || buf_.use_count() > 1) {
			auto new_buf = std::make_shared<buffer>(buf_ ? std::max(buf_->capacity, buf_->size) : 0);
			if (buf_ && buf_->size > 0) {
				new_buf->size = buf_->size;
				// Copy construct elements
				for (size_t i = 0; i < buf_->size; ++i) {
					new (new_buf->data + i) T(buf_->data[i]);
				}
			}
			buf_ = new_buf;
		}
	}

	void ensure_capacity(size_t min_cap) {
		ensure_unique();
		if (buf_->capacity < min_cap) {
			size_t new_cap = std::max(min_cap, buf_->capacity * 2);
			if (new_cap == 0)
				new_cap = 1;

			T* new_data = static_cast<T*>(aligned_alloc(alignof(T), sizeof(T) * new_cap));

			// Move construct elements to new buffer
			for (size_t i = 0; i < buf_->size; ++i) {
				new (new_data + i) T(std::move(buf_->data[i]));
				buf_->data[i].~T();
			}

			aligned_free(buf_->data);
			buf_->data = new_data;
			buf_->capacity = new_cap;
		}
	}

public:
	using value_type = T;
	using size_type = size_t;
	using difference_type = ptrdiff_t;
	using reference = T&;
	using const_reference = const T&;
	using pointer = T*;
	using const_pointer = const T*;

	// Iterator classes
	class iterator {
		T* ptr_;

	public:
		using iterator_category = std::random_access_iterator_tag;
		using value_type = T;
		using difference_type = ptrdiff_t;
		using pointer = T*;
		using reference = T&;

		iterator(T* ptr = nullptr) : ptr_(ptr) {}

		reference operator*() const { return *ptr_; }
		pointer operator->() const { return ptr_; }
		iterator& operator++() {
			++ptr_;
			return *this;
		}
		iterator operator++(int) {
			iterator tmp = *this;
			++ptr_;
			return tmp;
		}
		iterator& operator--() {
			--ptr_;
			return *this;
		}
		iterator operator--(int) {
			iterator tmp = *this;
			--ptr_;
			return tmp;
		}
		iterator& operator+=(difference_type n) {
			ptr_ += n;
			return *this;
		}
		iterator& operator-=(difference_type n) {
			ptr_ -= n;
			return *this;
		}
		iterator operator+(difference_type n) const { return iterator(ptr_ + n); }
		iterator operator-(difference_type n) const { return iterator(ptr_ - n); }
		difference_type operator-(const iterator& other) const { return ptr_ - other.ptr_; }
		reference operator[](difference_type n) const { return ptr_[n]; }

		bool operator==(const iterator& other) const { return ptr_ == other.ptr_; }
		bool operator!=(const iterator& other) const { return ptr_ != other.ptr_; }
		bool operator<(const iterator& other) const { return ptr_ < other.ptr_; }
		bool operator<=(const iterator& other) const { return ptr_ <= other.ptr_; }
		bool operator>(const iterator& other) const { return ptr_ > other.ptr_; }
		bool operator>=(const iterator& other) const { return ptr_ >= other.ptr_; }
	};

	class const_iterator {
		const T* ptr_;

	public:
		using iterator_category = std::random_access_iterator_tag;
		using value_type = T;
		using difference_type = ptrdiff_t;
		using pointer = const T*;
		using reference = const T&;

		const_iterator(const T* ptr = nullptr) : ptr_(ptr) {}
		const_iterator(const iterator& it) : ptr_(&(*it)) {}

		reference operator*() const { return *ptr_; }
		pointer operator->() const { return ptr_; }
		const_iterator& operator++() {
			++ptr_;
			return *this;
		}
		const_iterator operator++(int) {
			const_iterator tmp = *this;
			++ptr_;
			return tmp;
		}
		const_iterator& operator--() {
			--ptr_;
			return *this;
		}
		const_iterator operator--(int) {
			const_iterator tmp = *this;
			--ptr_;
			return tmp;
		}
		const_iterator& operator+=(difference_type n) {
			ptr_ += n;
			return *this;
		}
		const_iterator& operator-=(difference_type n) {
			ptr_ -= n;
			return *this;
		}
		const_iterator operator+(difference_type n) const { return const_iterator(ptr_ + n); }
		const_iterator operator-(difference_type n) const { return const_iterator(ptr_ - n); }
		difference_type operator-(const const_iterator& other) const { return ptr_ - other.ptr_; }
		reference operator[](difference_type n) const { return ptr_[n]; }

		bool operator==(const const_iterator& other) const { return ptr_ == other.ptr_; }
		bool operator!=(const const_iterator& other) const { return ptr_ != other.ptr_; }
		bool operator<(const const_iterator& other) const { return ptr_ < other.ptr_; }
		bool operator<=(const const_iterator& other) const { return ptr_ <= other.ptr_; }
		bool operator>(const const_iterator& other) const { return ptr_ > other.ptr_; }
		bool operator>=(const const_iterator& other) const { return ptr_ >= other.ptr_; }
	};

	// Constructors
	CowVector() : buf_(std::make_shared<buffer>()) {}

	explicit CowVector(size_type count) : buf_(std::make_shared<buffer>(count)) {
		buf_->size = count;
		for (size_t i = 0; i < count; ++i) {
			new (buf_->data + i) T();
		}
	}

	CowVector(size_type count, const T& value) : buf_(std::make_shared<buffer>(count)) {
		buf_->size = count;
		for (size_t i = 0; i < count; ++i) {
			new (buf_->data + i) T(value);
		}
	}

	CowVector(std::initializer_list<T> init) : buf_(std::make_shared<buffer>(init.size())) {
		buf_->size = init.size();
		size_t i = 0;
		for (const auto& item : init) {
			new (buf_->data + i++) T(item);
		}
	}

	template <typename InputIt>
	CowVector(InputIt first, InputIt last) {
		size_t count = std::distance(first, last);
		buf_ = std::make_shared<buffer>(count);
		buf_->size = count;
		size_t i = 0;
		for (auto it = first; it != last; ++it) {
			new (buf_->data + i++) T(*it);
		}
	}

	// Copy constructor - COW magic happens here!
	CowVector(const CowVector& other) : buf_(other.buf_) {}

	// Move constructor
	CowVector(CowVector&& other) noexcept : buf_(std::move(other.buf_)) {}

	// Assignment operators
	CowVector& operator=(const CowVector& other) {
		if (this != &other) {
			buf_ = other.buf_;
		}
		return *this;
	}

	CowVector& operator=(CowVector&& other) noexcept {
		if (this != &other) {
			buf_ = std::move(other.buf_);
		}
		return *this;
	}

	CowVector& operator=(std::initializer_list<T> ilist) {
		clear();
		reserve(ilist.size());
		for (const auto& item : ilist) {
			push_back(item);
		}
		return *this;
	}

	// Element access
	reference at(size_type pos) {
		if (pos >= size()) {
			throw std::out_of_range("cow_vector::at");
		}
		ensure_unique();
		return buf_->data[pos];
	}

	const_reference at(size_type pos) const {
		if (pos >= size()) {
			throw std::out_of_range("cow_vector::at");
		}
		return buf_->data[pos];
	}

	reference operator[](size_type pos) {
		ensure_unique();
		return buf_->data[pos];
	}

	const_reference operator[](size_type pos) const { return buf_->data[pos]; }

	reference front() {
		ensure_unique();
		return buf_->data[0];
	}

	const_reference front() const { return buf_->data[0]; }

	reference back() {
		ensure_unique();
		return buf_->data[buf_->size - 1];
	}

	const_reference back() const { return buf_->data[buf_->size - 1]; }

	T* data() {
		ensure_unique();
		return buf_->data;
	}

	const T* data() const noexcept { return buf_ ? buf_->data : nullptr; }

	// Iterators
	iterator begin() {
		ensure_unique();
		return iterator(buf_->data);
	}

	const_iterator begin() const noexcept { return const_iterator(buf_ ? buf_->data : nullptr); }

	const_iterator cbegin() const noexcept { return const_iterator(buf_ ? buf_->data : nullptr); }

	iterator end() {
		ensure_unique();
		return iterator(buf_->data + buf_->size);
	}

	const_iterator end() const noexcept { return const_iterator(buf_ ? buf_->data + buf_->size : nullptr); }

	const_iterator cend() const noexcept { return const_iterator(buf_ ? buf_->data + buf_->size : nullptr); }

	// Capacity
	bool empty() const noexcept { return !buf_ || buf_->size == 0; }

	size_type size() const noexcept { return buf_ ? buf_->size : 0; }

	size_type capacity() const noexcept { return buf_ ? buf_->capacity : 0; }

	void reserve(size_type new_cap) {
		if (new_cap > capacity()) {
			ensure_capacity(new_cap);
		}
	}

	void shrink_to_fit() {
		if (buf_ && buf_->capacity > buf_->size) {
			ensure_unique();
			if (buf_->size == 0) {
				if (buf_->data) {
					std::free(buf_->data);
					buf_->data = nullptr;
				}
				buf_->capacity = 0;
			}
			else {
				T* new_data = static_cast<T*>(aligned_alloc(alignof(T), sizeof(T) * buf_->size));
				for (size_t i = 0; i < buf_->size; ++i) {
					new (new_data + i) T(std::move(buf_->data[i]));
					buf_->data[i].~T();
				}
				std::free(buf_->data);
				buf_->data = new_data;
				buf_->capacity = buf_->size;
			}
		}
	}

	// Modifiers
	void clear() noexcept {
		if (buf_ && buf_->size > 0) {
			ensure_unique();
			for (size_t i = 0; i < buf_->size; ++i) {
				buf_->data[i].~T();
			}
			buf_->size = 0;
		}
	}

	void push_back(const T& value) {
		ensure_capacity(size() + 1);
		new (buf_->data + buf_->size) T(value);
		++buf_->size;
	}

	void push_back(T&& value) {
		ensure_capacity(size() + 1);
		new (buf_->data + buf_->size) T(std::move(value));
		++buf_->size;
	}

	template <typename... Args>
	reference emplace_back(Args&&... args) {
		ensure_capacity(size() + 1);
		T* ptr = buf_->data + buf_->size;
		new (ptr) T(std::forward<Args>(args)...);
		++buf_->size;
		return *ptr;
	}

	void pop_back() {
		if (buf_ && buf_->size > 0) {
			ensure_unique();
			--buf_->size;
			buf_->data[buf_->size].~T();
		}
	}

	void resize(size_type count) { resize(count, T()); }

	void resize(size_type count, const T& value) {
		if (count > size()) {
			ensure_capacity(count);
			for (size_t i = buf_->size; i < count; ++i) {
				new (buf_->data + i) T(value);
			}
		}
		else if (count < size()) {
			ensure_unique();
			for (size_t i = count; i < buf_->size; ++i) {
				buf_->data[i].~T();
			}
		}
		buf_->size = count;
	}

	// COW-specific method to check if buffer is shared
	bool is_shared() const noexcept { return buf_ && buf_.use_count() > 1; }

	size_t use_count() const noexcept { return buf_ ? buf_.use_count() : 0; }

	operator std::span<T>() const noexcept { return std::span(buf_->data, size()); }
};

// Non-member functions
template <typename T>
bool operator==(const CowVector<T>& lhs, const CowVector<T>& rhs) {
	if (lhs.size() != rhs.size())
		return false;
	return std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

template <typename T>
bool operator!=(const CowVector<T>& lhs, const CowVector<T>& rhs) {
	return !(lhs == rhs);
}

template <typename T>
bool operator<(const CowVector<T>& lhs, const CowVector<T>& rhs) {
	return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <typename T>
void swap(CowVector<T>& lhs, CowVector<T>& rhs) noexcept {
	std::swap(lhs.buf_, rhs.buf_);
}

} //namespace feather