#pragma once

#include <cstddef>
#include <iterator>
#include <utility>

namespace portable_detail {

// The shipping image statically emits the Visual Studio 2010 std::sort
// implementation. Host std::sort implementations choose different members
// of equal-key groups, so production paths whose result is observable use
// the target partition/insertion/heap sequence explicitly.
template <typename Iterator, typename Predicate>
void Msvc2010Swap(Iterator left, Iterator right, Predicate) {
    if (left == right) {
        return;
    }
    using value_type = typename std::iterator_traits<Iterator>::value_type;
    const value_type temporary = *left;
    *left = *right;
    *right = temporary;
}

template <typename Iterator, typename Predicate>
void Msvc2010MedianOfThree(Iterator first, Iterator middle, Iterator last, Predicate predicate) {
    if (predicate(*middle, *first)) {
        Msvc2010Swap(middle, first, predicate);
    }
    if (predicate(*last, *middle)) {
        Msvc2010Swap(last, middle, predicate);
    }
    if (predicate(*middle, *first)) {
        Msvc2010Swap(middle, first, predicate);
    }
}

template <typename Iterator, typename Predicate>
void Msvc2010ChooseMedian(Iterator first, Iterator middle, Iterator last, Predicate predicate) {
    Iterator firstMedian = first;
    Iterator lastMedian = last;
    if (last - first > 40) {
        const auto step = (last - first + 1) / 8;
        firstMedian = first + step;
        Msvc2010MedianOfThree(first, firstMedian, first + step * 2, predicate);
        Msvc2010MedianOfThree(middle - step, middle, middle + step, predicate);
        lastMedian = last - step;
        Msvc2010MedianOfThree(last - step * 2, lastMedian, last, predicate);
    }
    Msvc2010MedianOfThree(firstMedian, middle, lastMedian, predicate);
}

template <typename Iterator, typename Predicate>
std::pair<Iterator, Iterator> Msvc2010Partition(Iterator first, Iterator last, Predicate predicate) {
    Iterator middle = first + (last - first) / 2;
    Msvc2010ChooseMedian(first, middle, last - 1, predicate);

    Iterator pivotFirst = middle;
    Iterator pivotLast = pivotFirst + 1;
    while (first < pivotFirst && !predicate(*(pivotFirst - 1), *pivotFirst) && !predicate(*pivotFirst, *(pivotFirst - 1))) {
        --pivotFirst;
    }
    while (pivotLast < last && !predicate(*pivotLast, *pivotFirst) && !predicate(*pivotFirst, *pivotLast)) {
        ++pivotLast;
    }

    Iterator scanRight = pivotLast;
    Iterator scanLeft = pivotFirst;
    for (;;) {
        for (; scanRight < last; ++scanRight) {
            if (predicate(*scanRight, *pivotFirst)) {
                break;
            }
            if (!predicate(*pivotFirst, *scanRight)) {
                Msvc2010Swap(pivotLast, scanRight, predicate);
                ++pivotLast;
            }
        }
        for (; first < scanLeft; --scanLeft) {
            if (predicate(*pivotFirst, *(scanLeft - 1))) {
                break;
            }
            if (!predicate(*(scanLeft - 1), *pivotFirst)) {
                --pivotFirst;
                Msvc2010Swap(pivotFirst, scanLeft - 1, predicate);
            }
        }

        if (scanLeft == first && scanRight == last) {
            return {pivotFirst, pivotLast};
        }
        if (scanLeft == first) {
            if (pivotLast != scanRight) {
                Msvc2010Swap(pivotFirst, pivotLast, predicate);
            }
            ++pivotLast;
            Msvc2010Swap(pivotFirst, scanRight, predicate);
            ++pivotFirst;
            ++scanRight;
        } else if (scanRight == last) {
            --scanLeft;
            --pivotFirst;
            Msvc2010Swap(scanLeft, pivotFirst, predicate);
            --pivotLast;
            Msvc2010Swap(pivotFirst, pivotLast, predicate);
        } else {
            --scanLeft;
            Msvc2010Swap(scanRight, scanLeft, predicate);
            ++scanRight;
        }
    }
}

template <typename Iterator, typename Predicate>
void Msvc2010InsertionSort(Iterator first, Iterator last, Predicate predicate) {
    if (first == last) {
        return;
    }
    using value_type = typename std::iterator_traits<Iterator>::value_type;
    for (Iterator next = first + 1; next != last; ++next) {
        const value_type value = *next;
        if (predicate(value, *first)) {
            for (Iterator destination = next; destination != first; --destination) {
                *destination = *(destination - 1);
            }
            *first = value;
            continue;
        }
        Iterator destination = next;
        while (predicate(value, *(destination - 1))) {
            *destination = *(destination - 1);
            --destination;
        }
        *destination = value;
    }
}

template <typename Iterator, typename Difference, typename Value, typename Predicate>
void Msvc2010PushHeap(Iterator first, Difference hole, Difference top, const Value& value, Predicate predicate) {
    while (top < hole) {
        const Difference parent = (hole - 1) / 2;
        if (!predicate(first[parent], value)) {
            break;
        }
        first[hole] = first[parent];
        hole = parent;
    }
    first[hole] = value;
}

template <typename Iterator, typename Difference, typename Value, typename Predicate>
void Msvc2010AdjustHeap(Iterator first, Difference hole, Difference count, const Value& value, Predicate predicate) {
    const Difference top = hole;
    Difference secondChild = hole * 2 + 2;
    while (secondChild < count) {
        if (predicate(first[secondChild], first[secondChild - 1])) {
            --secondChild;
        }
        first[hole] = first[secondChild];
        hole = secondChild;
        secondChild = hole * 2 + 2;
    }
    if (secondChild == count) {
        first[hole] = first[count - 1];
        hole = count - 1;
    }
    Msvc2010PushHeap(first, hole, top, value, predicate);
}

template <typename Iterator, typename Predicate>
void Msvc2010HeapSort(Iterator first, Iterator last, Predicate predicate) {
    using difference_type = typename std::iterator_traits<Iterator>::difference_type;
    using value_type = typename std::iterator_traits<Iterator>::value_type;
    const difference_type count = last - first;
    for (difference_type hole = count / 2; 0 < hole;) {
        --hole;
        const value_type value = first[hole];
        Msvc2010AdjustHeap(first, hole, count, value, predicate);
    }
    for (Iterator heapLast = last; 1 < heapLast - first;) {
        const value_type value = *(heapLast - 1);
        *(heapLast - 1) = *first;
        --heapLast;
        Msvc2010AdjustHeap(first, difference_type{0}, heapLast - first, value, predicate);
    }
}

template <typename Iterator, typename Predicate>
void Msvc2010SortImpl(
    Iterator first, Iterator last, typename std::iterator_traits<Iterator>::difference_type ideal, Predicate predicate
) {
    auto count = last - first;
    if (32 < count) {
        do {
            if (ideal < 1) {
                break;
            }
            const auto middle = Msvc2010Partition(first, last, predicate);
            ideal = ideal / 2 + (ideal / 2) / 2;
            if (middle.first - first < last - middle.second) {
                Msvc2010SortImpl(first, middle.first, ideal, predicate);
                first = middle.second;
            } else {
                Msvc2010SortImpl(middle.second, last, ideal, predicate);
                last = middle.first;
            }
            count = last - first;
        } while (32 < count);
        if (32 < count) {
            Msvc2010HeapSort(first, last, predicate);
            return;
        }
    }
    if (1 < count) {
        Msvc2010InsertionSort(first, last, predicate);
    }
}

template <typename Iterator, typename Predicate>
void Msvc2010Sort(Iterator first, Iterator last, Predicate predicate) {
    Msvc2010SortImpl(first, last, last - first, predicate);
}

} // namespace portable_detail
