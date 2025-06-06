// Copyright (C) 2005 - 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "DescIdx.h"
#include "DescriptionVector.h"
#include "helpers/containerUtils.h"
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

/// Hold describing data about a type with access by name and index
template<typename T>
struct DescriptionContainer
{
    /// Add a new description. Throws if one with the same name already exists
    DescIdx<T> add(T desc);
    /// Return the number of descriptions stored
    unsigned size() const { return static_cast<unsigned>(items.size()); }
    /// Return the index of the item with the given name
    DescIdx<T> getIndex(const std::string& name) const;
    /// Return the entry with the given name or nullptr
    const T* tryGet(const std::string& name) const;
    /// Return the entry with the given idx or nullptr
    const T* tryGet(DescIdx<T> idx) const;
    /// Return the item at the given index
    const T& get(DescIdx<T> idx) const;
    /// Return a mutable reference to the given item
    T& getMutable(DescIdx<T> idx);
    /// Return the first item matching the predicate
    template<class Predicate>
    constexpr DescIdx<T> find(Predicate&& predicate) const;
    /// Return all items matching the predicate
    template<class Predicate>
    std::vector<DescIdx<T>> findAll(Predicate&& predicate) const;

    auto begin() const { return items.begin(); }
    auto end() const { return items.end(); }

private:
    DescriptionVector<T, T> items;
    std::map<std::string, unsigned> name2Idx;
};

template<typename T>
inline DescIdx<T> DescriptionContainer<T>::add(T desc)
{
    if(!getIndex(desc.name))
    {
        if(size() >= DescIdx<T>::INVALID)
            throw std::runtime_error("To many entries!");
        DescIdx<T> idx(size());
        items.push_back(desc);
        name2Idx[desc.name] = idx.value;
        return idx;
    }
    throw std::runtime_error(std::string("Duplicate entry with name ") + desc.name + " added!");
}

template<typename T>
inline DescIdx<T> DescriptionContainer<T>::getIndex(const std::string& name) const
{
    auto it = name2Idx.find(name);
    if(it == name2Idx.end())
        return DescIdx<T>();
    return DescIdx<T>(it->second);
}

template<typename T>
inline const T* DescriptionContainer<T>::tryGet(const DescIdx<T> idx) const
{
    if(!idx || idx.value >= size())
        return nullptr;
    return &items[idx];
}

template<typename T>
inline const T* DescriptionContainer<T>::tryGet(const std::string& name) const
{
    return tryGet(getIndex(name));
}

template<typename T>
inline const T& DescriptionContainer<T>::get(const DescIdx<T> idx) const
{
    return items[idx];
}

template<typename T>
inline T& DescriptionContainer<T>::getMutable(const DescIdx<T> idx)
{
    return items[idx];
}

template<typename T>
template<class Predicate>
constexpr DescIdx<T> DescriptionContainer<T>::find(Predicate&& predicate) const
{
    const auto idx = helpers::indexOf_if(items, std::forward<Predicate>(predicate));
    return idx >= 0 ? DescIdx<T>(static_cast<typename DescIdx<T>::value_type>(idx)) : DescIdx<T>();
}

template<typename T>
template<class Predicate>
std::vector<DescIdx<T>> DescriptionContainer<T>::findAll(Predicate&& predicate) const
{
    std::vector<DescIdx<T>> result;
    for(const auto i : items.indices())
    {
        if(predicate(items[i]))
            result.push_back(i);
    }
    return result;
}