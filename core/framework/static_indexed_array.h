#pragma once

#include "assert.h"

#include <cstdint>

#include <deque>
#include <optional>
#include <vector>

namespace feather {
// The elements of this class will stay at the same index in the container during their existence
template <class T>
class StaticIndexedArray {
	std::vector<std::optional<T>> _elements;
	std::deque<size_t> _free_list;

	bool _is_free(size_t i) const;
	bool _is_index_valid(size_t i) const;
	void _clean_free_slots();
	size_t _get_first_element_index() const;

public:
	size_t add(T&&);
	size_t add(const T&);
	void remove(size_t i);
	size_t size() const;
	bool empty() const;
	void clear();
	void reserve(size_t size);
	template <class... TArgs>
	size_t emplace(TArgs... args);
	bool has_value(size_t i) const { return _is_index_valid(i); }

	T& operator[](size_t i);
	const T& at(size_t i) const;
	T& at(int i);

	class Iterator {
		using VecIt = std::optional<T>*;
		VecIt it, lowerBound, upperBound, first;
		Iterator(VecIt it, VecIt lowerBound, VecIt upperBound, VecIt first);

	public:
		T& operator*();
		Iterator operator++();
		Iterator operator++(int);
		Iterator operator--();
		Iterator operator--(int);

		size_t AbsoluteIndex() { return static_cast<size_t>(std::distance(first, it)); }

		friend bool operator==(const Iterator& a, const Iterator& b) { return a.it == b.it; }

		T* operator->() { return &**it; }

		friend StaticIndexedArray;
	};

	class ConstIterator {
		using VecIt = const std::optional<T>*;
		VecIt it, lowerBound, upperBound, first;
		ConstIterator(VecIt it, VecIt lowerBound, VecIt upperBound, VecIt first);

	public:
		const T& operator*() const;
		ConstIterator operator++();
		ConstIterator operator++(int);
		ConstIterator operator--();
		ConstIterator operator--(int);

		friend bool operator==(const ConstIterator& a, const ConstIterator& b) { return a.it == b.it; }

		T* operator->() { return &**it; }

		std::uint32_t absolute_index() { return std::distance(first, it); }

		friend StaticIndexedArray;
	};

	Iterator begin();
	Iterator end();
	ConstIterator begin() const;
	ConstIterator end() const;
};

template <class T>
bool StaticIndexedArray<T>::_is_free(size_t i) const {
	return !_elements[i].has_value();
}

template <class T>
bool StaticIndexedArray<T>::_is_index_valid(size_t i) const {
	return i < _elements.size() && !_is_free(i);
}

template <class T>
void StaticIndexedArray<T>::_clean_free_slots() {
	auto rIt = _elements.rbegin();
	while (rIt != _elements.rend() && !rIt->has_value())
		++rIt;

	size_t nbItemsToRemove = std::distance(_elements.rbegin(), rIt);

	_free_list.resize(_free_list.size() - nbItemsToRemove);
	_elements.resize(_elements.size() - nbItemsToRemove);
}

template <class T>
size_t StaticIndexedArray<T>::_get_first_element_index() const {
	int current = 0;
	while (!_free_list.empty() && current < _free_list.size() && _free_list[current] == current)
		current++;
	return current;
}

template <class T>
size_t StaticIndexedArray<T>::add(T&& element) {
	size_t index;
	if (_free_list.empty()) {
		_elements.push_back(std::move(element));
		index = _elements.size() - 1;
	}
	else {
		index = _free_list.front();
		_elements[index] = std::move(element);
		_free_list.pop_front();
	}
	return index;
}

template <class T>
size_t StaticIndexedArray<T>::add(const T& element) {
	size_t index;
	if (_free_list.empty()) {
		_elements.push_back(element);
		index = _elements.size() - 1;
	}
	else {
		index = _free_list.front();
		_elements[index] = element;
		_free_list.pop_front();
	}
	return index;
}

template <class T>
void StaticIndexedArray<T>::remove(size_t i) {
	if (!_is_index_valid(i))
		throw std::out_of_range { "No element at that index" };

	_elements[i] = {};
	_free_list.insert(std::lower_bound(_free_list.begin(), _free_list.end(), i), i);

	if (i == _elements.size() - 1) {
		_clean_free_slots();
	}
}

template <class T>
size_t StaticIndexedArray<T>::size() const {
	return _elements.size() - _free_list.size();
}

template <class T>
bool StaticIndexedArray<T>::empty() const {
	return size() == 0;
}

template <class T>
void StaticIndexedArray<T>::clear() {
	_elements.clear();
	_free_list.clear();
}

template <class T>
void StaticIndexedArray<T>::reserve(size_t size) {
	_elements.reserve(size);
}

template <class T>
template <class... TArgs>
size_t StaticIndexedArray<T>::emplace(TArgs... args) {
	return add(T { std::forward<TArgs>(args)... });
}

template <class T>
T& StaticIndexedArray<T>::operator[](size_t i) {
	return *_elements[i];
}

template <class T>
const T& StaticIndexedArray<T>::at(size_t i) const {
	fassert(!_is_index_valid(i));
	return *_elements[i];
}

template <class T>
T& StaticIndexedArray<T>::at(int i) {
	fassert(!_is_index_valid(i));
	return *_elements[i];
}

template <class T>
StaticIndexedArray<T>::Iterator::Iterator(VecIt it, VecIt lowerBound, VecIt upperBound, VecIt first)
		: it { it }
		, lowerBound { lowerBound }
		, upperBound { upperBound }
		, first { first } {
}

template <class T>
StaticIndexedArray<T>::ConstIterator::ConstIterator(VecIt it, VecIt lowerBound, VecIt upperBound, VecIt first)
		: it { it }
		, lowerBound { lowerBound }
		, upperBound { upperBound }
		, first { first } {
}

template <class T>
T& StaticIndexedArray<T>::Iterator::operator*() {
	return **it;
}

template <class T>
typename StaticIndexedArray<T>::Iterator StaticIndexedArray<T>::Iterator::operator++() {
	do
		++it;
	while (it != upperBound && !it->has_value());
	return *this;
}

template <class T>
typename StaticIndexedArray<T>::Iterator StaticIndexedArray<T>::Iterator::operator++(int) {
	auto prev = *this;
	do
		++it;
	while (it != upperBound && !it->has_value());
	return prev;
}

template <class T>
typename StaticIndexedArray<T>::Iterator StaticIndexedArray<T>::Iterator::operator--() {
	do
		--it;
	while (it != lowerBound && !it->has_value());
	return *this;
}

template <class T>
typename StaticIndexedArray<T>::Iterator StaticIndexedArray<T>::Iterator::operator--(int) {
	auto prev = *this;
	do
		--it;
	while (it != lowerBound && !it->has_value());
	return prev;
}

template <class T>
const T& StaticIndexedArray<T>::ConstIterator::operator*() const {
	return **it;
}

template <class T>
typename StaticIndexedArray<T>::ConstIterator StaticIndexedArray<T>::ConstIterator::operator++() {
	do
		++it;
	while (it != upperBound && !it->has_value());
	return *this;
}

template <class T>
typename StaticIndexedArray<T>::ConstIterator StaticIndexedArray<T>::ConstIterator::operator++(int) {
	auto prev = *this;
	do
		++it;
	while (it != upperBound && !it->has_value());
	return prev;
}

template <class T>
typename StaticIndexedArray<T>::ConstIterator StaticIndexedArray<T>::ConstIterator::operator--() {
	do
		--it;
	while (it != lowerBound && !it->has_value());
	return *this;
}

template <class T>
typename StaticIndexedArray<T>::ConstIterator StaticIndexedArray<T>::ConstIterator::operator--(int) {
	auto prev = *this;
	do
		--it;
	while (it != lowerBound && !it->has_value());
	return prev;
}

template <class T>
typename StaticIndexedArray<T>::Iterator StaticIndexedArray<T>::begin() {
	int current = 0;
	while (!_free_list.empty() && current < _free_list.size() && _free_list[current] == current)
		current++;

	return Iterator { _elements.data() + current,
					  _elements.data() + current - 1,
					  _elements.data() + std::max(_elements.size() - 1ULL, 0ULL) + 1,
					  _elements.data() };
}

template <class T>
typename StaticIndexedArray<T>::Iterator StaticIndexedArray<T>::end() {
	auto it = _elements.data() + std::max(_elements.size() - 1ULL, 0ULL) + 1;
	return Iterator { it, begin().lowerBound, it, _elements.data() };
}

template <class T>
typename StaticIndexedArray<T>::ConstIterator StaticIndexedArray<T>::begin() const {
	int current = 0;
	while (!_free_list.empty() && current < _free_list.size() && _free_list[current] == current)
		current++;

	return ConstIterator { _elements.data() + current,
						   _elements.data() + current - 1,
						   _elements.data() + std::max(_elements.size() - 1, 0ULL) + 1,
						   _elements.data() };
}

template <class T>
typename StaticIndexedArray<T>::ConstIterator StaticIndexedArray<T>::end() const {
	const auto beforeBegin = _elements.data() + _get_first_element_index();
	const auto* endPtr = _elements.data() + std::max(_elements.size() - 1, 0ULL) + 1;
	return ConstIterator { endPtr, beforeBegin - 1, endPtr, _elements.begin() };
}

} // namespace feather