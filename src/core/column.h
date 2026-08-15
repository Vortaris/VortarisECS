#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <godot_cpp/core/memory.hpp>

namespace vortaris {

// Aligned column buffer storing contiguous rows of a single component type.
//
// Rows are `stride_` bytes apart and every row starts at an address aligned to
// `elem_align_` (16 bytes when the component contains Vector4/Transform3D).
// Rows are trivially copyable so moves and swaps are plain memcpy.
class Column {
public:
	Column() = default;
	~Column() { free_buf(); }

	Column(const Column &) = delete;
	Column &operator=(const Column &) = delete;

	Column(Column &&p_other) noexcept { *this = std::move(p_other); }
	Column &operator=(Column &&p_other) noexcept {
		if (this != &p_other) {
			free_buf();
			data_ = p_other.data_;
			alloc_base_ = p_other.alloc_base_;
			capacity_ = p_other.capacity_;
			size_ = p_other.size_;
			elem_size_ = p_other.elem_size_;
			elem_align_ = p_other.elem_align_;
			stride_ = p_other.stride_;
			versions_ = std::move(p_other.versions_);
			tracking_ = p_other.tracking_;
			max_version_ = p_other.max_version_;
			p_other.data_ = nullptr;
			p_other.alloc_base_ = nullptr;
			p_other.capacity_ = 0;
			p_other.size_ = 0;
			p_other.elem_size_ = 0;
			p_other.elem_align_ = 1;
			p_other.stride_ = 0;
			p_other.tracking_ = false;
			p_other.max_version_ = 0;
		}
		return *this;
	}

	void init(size_t p_elem_size, size_t p_elem_align) {
		free_buf();
		elem_size_ = p_elem_size;
		elem_align_ = p_elem_align > 0 ? p_elem_align : 1;
		stride_ = (elem_size_ + elem_align_ - 1) / elem_align_ * elem_align_;
		size_ = 0;
		capacity_ = 0;
	}

	void *row(size_t i) { return data_ + i * stride_; }
	const void *row(size_t i) const { return data_ + i * stride_; }

	void reserve(size_t n) {
		if (n > capacity_) {
			realloc_to(n);
		}
	}
	void grow() {
		if (size_ == capacity_) {
			realloc_to(capacity_ == 0 ? 8 : capacity_ * 2);
		}
		++size_;
		if (tracking_) {
			versions_.push_back(0);
		}
	}
	void append(const void *p_src) {
		grow();
		std::memcpy(row(size_ - 1), p_src, elem_size_);
	}
	void swap_remove(size_t i) {
		if (i + 1 < size_) {
			std::memcpy(row(i), row(size_ - 1), elem_size_);
			if (tracking_) {
				versions_[i] = versions_.back();
			}
		}
		--size_;
		if (tracking_) {
			versions_.pop_back();
			// Conservative downgrade: the row that held the max version may have
			// been removed, so force the next change query to re-scan.
			max_version_ = 0;
		}
	}
	void pop_back() {
		if (size_ == 0) {
			return;
		}
		--size_;
		if (tracking_) {
			versions_.pop_back();
			max_version_ = 0;
		}
	}
	void clear() {
		size_ = 0;
		if (tracking_) {
			versions_.clear();
			max_version_ = 0;
		}
	}

	size_t size() const { return size_; }
	size_t capacity() const { return capacity_; }
	size_t elem_size() const { return elem_size_; }
	size_t elem_align() const { return elem_align_; }

	// --- change tracking (lazily allocated, only for .changed() queries) ---
	// Rows that predate the first enable are stamped with `p_tick` rather than
	// 0: a change query whose baseline was set earlier (or starts at 0) must
	// report pre-enable rows once, otherwise every write that happened before
	// tracking was turned on is silently lost.
	void ensure_versions(uint64_t p_tick) {
		if (!tracking_) {
			tracking_ = true;
			versions_.assign(size_, p_tick);
			max_version_ = p_tick;
		}
	}
	void mark_changed(size_t i, uint64_t p_tick) {
		if (tracking_ && i < versions_.size()) {
			versions_[i] = p_tick;
			if (p_tick > max_version_) {
				max_version_ = p_tick;
			}
		}
	}
	void set_version(size_t i, uint64_t p_version) {
		if (tracking_ && i < versions_.size()) {
			versions_[i] = p_version;
			if (p_version > max_version_) {
				max_version_ = p_version;
			}
		}
	}
	bool row_changed_since(size_t i, uint64_t p_baseline) const {
		return tracking_ && i < versions_.size() && versions_[i] > p_baseline;
	}
	uint64_t version_at(size_t i) const {
		return tracking_ && i < versions_.size() ? versions_[i] : 0;
	}
	bool has_versions() const { return tracking_; }
	// Highest version currently in the column (used by ChangeView to skip an
	// archetype whose watched columns were not touched since a baseline).
	uint64_t max_version() const { return tracking_ ? max_version_ : 0; }

private:
	void free_buf() {
		if (alloc_base_) {
			memfree(alloc_base_);
			alloc_base_ = nullptr;
		}
		data_ = nullptr;
		capacity_ = 0;
		size_ = 0;
		versions_.clear();
		tracking_ = false;
		max_version_ = 0;
	}
	void realloc_to(size_t p_new_cap) {
		size_t bytes = p_new_cap * stride_;
		void *base = memalloc(bytes + elem_align_);
		uintptr_t raw = reinterpret_cast<uintptr_t>(base);
		uintptr_t aligned = (raw + elem_align_ - 1) & ~(static_cast<uintptr_t>(elem_align_ - 1));
		std::byte *data = reinterpret_cast<std::byte *>(aligned);
		if (data_) {
			std::memcpy(data, data_, size_ * stride_);
		}
		if (alloc_base_) {
			memfree(alloc_base_);
		}
		alloc_base_ = static_cast<std::byte *>(base);
		data_ = data;
		capacity_ = p_new_cap;
	}

	std::byte *data_ = nullptr;
	std::byte *alloc_base_ = nullptr;
	size_t capacity_ = 0;
	size_t size_ = 0;
	size_t elem_size_ = 0;
	size_t elem_align_ = 1;
	size_t stride_ = 0;
	std::vector<uint64_t> versions_;
	uint64_t max_version_ = 0;
	bool tracking_ = false;
};

} // namespace vortaris
