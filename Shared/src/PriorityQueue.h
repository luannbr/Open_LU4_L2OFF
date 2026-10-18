#pragma once
#include <functional>
#include <utility>
#include <vector>

namespace runtime {
// Heap ordering is gameplay behavior: equal children select the right child.
// Standard vector storage. No emulated MSVC layout or allocator padding.
template <typename T, typename Container = std::vector<T>, typename Compare = std::less<T>>
class PriorityQueue {
public:
    using value_type = T;
    using container_type = Container;
    using value_compare = Compare;
    using size_type = typename container_type::size_type;
    using reference = value_type&;
    using const_reference = const value_type&;

    PriorityQueue() = default;

    explicit PriorityQueue(const Compare& compare) : comp(compare) {}

    bool empty() const noexcept { return c.empty(); }

    size_type size() const noexcept { return c.size(); }

    const_reference top() const { return c.front(); }

    void push(const value_type& value) {
        c.push_back(value);
        PushHeapMsvc2005();
    }

    void push(value_type&& value) {
        c.push_back(std::move(value));
        PushHeapMsvc2005();
    }

    void clear() noexcept { c.clear(); }

    void pop() {
        PopHeapMsvc2005();
        c.pop_back();
    }

protected:
    container_type c;
    Compare comp{};

    // Current L2NPC.exe 0x004249e4 rebuilds CDesirePQ with the same
    // VS2010 _Make_heap/_Adjust_heap ordering used by this adaptor's pop.
    // Keep it on the recovered adaptor so derived queues do not delegate a
    // production ordering decision to the host standard library.
    void RebuildHeapTargetOrder() {
        const size_type count = c.size();
        size_type parent = count / 2;
        while (parent != 0) {
            --parent;
            const value_type value = c[parent];
            AdjustHeapTargetOrder(parent, parent, count, value);
        }
    }

private:
    // The shipping binaries use the VS2005 _Push_heap/_Adjust_heap
    // implementation. A host STL may choose a different child when both
    // compare equal, which changes the dispatch order of CIOTimer entries
    // sharing one due tick. Preserve the exact VS2005 choice here: push does
    // not cross an equal parent and adjust selects the right equal child.
    void PushHeapMsvc2005() {
        const size_type count = c.size();
        if (count < 2) {
            return;
        }

        size_type hole = count - 1;
        const size_type top = 0;
        const value_type value = c[hole];
        PushHeapMsvc2005(hole, top, value);
    }

    void PushHeapMsvc2005(size_type hole, size_type top, const value_type& value) {
        while (top < hole) {
            const size_type parent = (hole - 1) / 2;
            if (!comp(c[parent], value)) {
                break;
            }
            c[hole] = c[parent];
            hole = parent;
        }
        c[hole] = value;
    }

    void PopHeapMsvc2005() {
        const size_type count = c.size();
        if (count < 2) {
            return;
        }

        const size_type bottom = count - 1;
        const value_type value = c[bottom];
        c[bottom] = c[0];

        const size_type top = 0;
        AdjustHeapTargetOrder(top, top, bottom, value);
    }

    void AdjustHeapTargetOrder(size_type hole, size_type top, size_type bottom, const value_type& value) {
        size_type secondChild = hole * 2 + 2;
        while (secondChild < bottom) {
            if (comp(c[secondChild], c[secondChild - 1])) {
                --secondChild;
            }
            c[hole] = c[secondChild];
            hole = secondChild;
            secondChild = hole * 2 + 2;
        }
        if (secondChild == bottom) {
            c[hole] = c[bottom - 1];
            hole = bottom - 1;
        }
        PushHeapMsvc2005(hole, top, value);
    }
};

} // namespace runtime
