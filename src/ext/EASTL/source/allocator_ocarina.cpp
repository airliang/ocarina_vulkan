//
// Created by Mike Smith on 2021/12/24.
//

#include <EASTL/allocator.h>
#include <EASTL/internal/config.h>

#include "core/ocarina_config.h"

#ifdef EASTL_MIMALLOC_ENABLED
#include <mimalloc.h>
#else
#include <cstdlib>
#endif

#if defined(OCARINA_ENABLE_TRACY) && OCARINA_ENABLE_TRACY
#include <tracy/Tracy.hpp>
#endif

namespace eastl
{

	namespace detail
	{
		inline static allocator*& GetDefaultAllocatorRef() noexcept
		{
			static allocator a;
			static allocator* pa = &a;
			return pa;
		}

		inline const char*& TracyMemoryPoolRef() noexcept
		{
			static thread_local const char* pool = nullptr;
			return pool;
		}

		// Callstacks help attribute Default-pool blocks; named pools show as separate Memory series.
		constexpr int kTracyAllocCallstackDepth = 32;

		inline void tracy_alloc(void* ptr, size_t size) noexcept
		{
#if defined(OCARINA_ENABLE_TRACY) && OCARINA_ENABLE_TRACY
			if (ptr == nullptr || size == 0) {
				return;
			}
			if (const char* pool = TracyMemoryPoolRef()) {
				TracyAllocNS(ptr, size, kTracyAllocCallstackDepth, pool);
			} else {
				TracyAllocS(ptr, size, kTracyAllocCallstackDepth);
			}
#else
			(void)ptr;
			(void)size;
#endif
		}

		inline void tracy_free(void* ptr) noexcept
		{
#if defined(OCARINA_ENABLE_TRACY) && OCARINA_ENABLE_TRACY
			if (ptr == nullptr) {
				return;
			}
			if (const char* pool = TracyMemoryPoolRef()) {
				TracyFreeNS(ptr, kTracyAllocCallstackDepth, pool);
			} else {
				TracyFreeS(ptr, kTracyAllocCallstackDepth);
			}
#else
			(void)ptr;
#endif
		}
	} // namespace detail

	EASTL_API allocator* GetDefaultAllocator()
	{
		return detail::GetDefaultAllocatorRef();
	}

	EASTL_API allocator* SetDefaultAllocator(allocator* pAllocator)
	{
		allocator* const pPrevAllocator = GetDefaultAllocator();
		detail::GetDefaultAllocatorRef() = pAllocator;
		return pPrevAllocator;
	}

	EASTL_API void SetTracyMemoryPool(const char* pool_name) noexcept
	{
		detail::TracyMemoryPoolRef() = pool_name;
	}

	EASTL_API const char* GetTracyMemoryPool() noexcept
	{
		return detail::TracyMemoryPoolRef();
	}


	void* allocator::allocate(size_t n, int /* flags */)
	{
#ifdef EASTL_MIMALLOC_ENABLED
		void* ptr = mi_malloc(n);
#else
		void* ptr = malloc(n);
#endif
		detail::tracy_alloc(ptr, n);
		return ptr;
	}


	void* allocator::allocate(size_t n, size_t alignment, size_t offset [[maybe_unused]], int flags)
	{
		EASTL_ASSERT(offset == 0u);
		if (alignment < EASTL_SYSTEM_ALLOCATOR_MIN_ALIGNMENT) {
			return allocate(n, flags);
		}

#ifdef EASTL_MIMALLOC_ENABLED
		void* ptr = mi_aligned_alloc(alignment, n);
#else
		void* ptr = aligned_alloc(alignment, n);
#endif
		detail::tracy_alloc(ptr, n);
		return ptr;
	}


	void allocator::deallocate(void* p, size_t)
	{
		detail::tracy_free(p);
#ifdef EASTL_MIMALLOC_ENABLED
		mi_free(p);
#else
		free(p);
#endif
	}

} // namespace eastl
