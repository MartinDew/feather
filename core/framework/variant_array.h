#pragma once
#include "container_utils.h"
#include <cstddef>
#include <initializer_list>
#include <memory>

namespace feather {

class Variant;

class VariantArray {
	class Buffer;
	std::unique_ptr<Buffer> _buffer;

	void ensure_unique();

public:
	using size_type = size_t;
	using difference_type = ptrdiff_t;

	// Iterator forward declarations
	class iterator;
	class const_iterator;

	// Constructors
	VariantArray();
	explicit VariantArray(size_type count);
	VariantArray(size_type count, const Variant& value);
	VariantArray(std::initializer_list<Variant> ilist);

	// Constructor from iterator range (type-erased)
	template <class TIterator>
	VariantArray(TIterator first, TIterator last) : VariantArray() {
		reserve(std::distance(first, last));
		for (auto it = first; it != last; ++it) {
			push_back(*it);
		}
	}

	// Copy and move
	VariantArray(const VariantArray& other);
	VariantArray(VariantArray&& other) noexcept;
	VariantArray& operator=(const VariantArray& other);
	VariantArray& operator=(VariantArray&& other) noexcept;
	VariantArray& operator=(std::initializer_list<Variant> ilist);

	~VariantArray();

	// Element access
	Variant& at(size_type pos);
	const Variant& at(size_type pos) const;
	Variant& operator[](size_type pos);
	const Variant& operator[](size_type pos) const;
	Variant& front();
	const Variant& front() const;
	Variant& back();
	const Variant& back() const;

	// Iterators
	iterator begin();
	const_iterator begin() const noexcept;
	const_iterator cbegin() const noexcept;
	iterator end();
	const_iterator end() const noexcept;
	const_iterator cend() const noexcept;

	// Capacity
	bool empty() const noexcept;
	size_type size() const noexcept;
	size_type capacity() const noexcept;
	void reserve(size_type new_cap);
	void shrink_to_fit();

	// Modifiers
	void clear() noexcept;
	void push_back(const Variant& value);
	void push_back(Variant&& value);

	template <typename... Args>
	Variant& emplace_back(Args&&... args);

	void pop_back();
	void resize(size_type count);
	void resize(size_type count, const Variant& value);

	// COW-specific
	bool is_shared() const noexcept;
	size_t use_count() const noexcept;

	// Comparison
	bool operator==(const VariantArray& other) const;
	bool operator!=(const VariantArray& other) const;
};

// Iterator classes
class VariantArray::iterator {
	Variant* ptr_;
	friend class VariantArray;

public:
	using iterator_category = std::random_access_iterator_tag;
	using value_type = Variant;
	using difference_type = ptrdiff_t;
	using pointer = Variant*;
	using reference = Variant&;

	iterator(Variant* ptr = nullptr);

	reference operator*() const;
	pointer operator->() const;
	iterator& operator++();
	iterator operator++(int);
	iterator& operator--();
	iterator operator--(int);
	iterator& operator+=(difference_type n);
	iterator& operator-=(difference_type n);
	iterator operator+(difference_type n) const;
	iterator operator-(difference_type n) const;
	difference_type operator-(const iterator& other) const;
	reference operator[](difference_type n) const;

	bool operator==(const iterator& other) const;
	bool operator!=(const iterator& other) const;
	bool operator<(const iterator& other) const;
	bool operator<=(const iterator& other) const;
	bool operator>(const iterator& other) const;
	bool operator>=(const iterator& other) const;
};

class VariantArray::const_iterator {
	const Variant* ptr_;
	friend class VariantArray;

public:
	using iterator_category = std::random_access_iterator_tag;
	using value_type = Variant;
	using difference_type = ptrdiff_t;
	using pointer = const Variant*;
	using reference = const Variant&;

	const_iterator(const Variant* ptr = nullptr);
	const_iterator(const iterator& it);

	reference operator*() const;
	pointer operator->() const;
	const_iterator& operator++();
	const_iterator operator++(int);
	const_iterator& operator--();
	const_iterator operator--(int);
	const_iterator& operator+=(difference_type n);
	const_iterator& operator-=(difference_type n);
	const_iterator operator+(difference_type n) const;
	const_iterator operator-(difference_type n) const;
	difference_type operator-(const const_iterator& other) const;
	reference operator[](difference_type n) const;

	bool operator==(const const_iterator& other) const;
	bool operator!=(const const_iterator& other) const;
	bool operator<(const const_iterator& other) const;
	bool operator<=(const const_iterator& other) const;
	bool operator>(const const_iterator& other) const;
	bool operator>=(const const_iterator& other) const;
};

} // namespace feather