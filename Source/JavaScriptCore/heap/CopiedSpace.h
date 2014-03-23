/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef CopiedSpace_h
#define CopiedSpace_h

#include "HeapBlock.h"
#include "TinyBloomFilter.h"
#include <wtf/Assertions.h>
#include <wtf/CheckedBoolean.h>
#include <wtf/DoublyLinkedList.h>
#include <wtf/HashSet.h>
#include <wtf/OSAllocator.h>
#include <wtf/PageAllocationAligned.h>
#include <wtf/StdLibExtras.h>
#include <wtf/ThreadingPrimitives.h>

namespace JSC {

class Heap;
class CopiedBlock;
class HeapBlock;

class CopiedSpace {
    friend class SlotVisitor;
public:
    CopiedSpace(Heap*);
    void init();

    CheckedBoolean tryAllocate(size_t, void**);
    CheckedBoolean tryReallocate(void**, size_t, size_t);
    
    void startedCopying();
    void doneCopying();
    bool isInCopyPhase() { return m_inCopyingPhase; }

    void pin(CopiedBlock*);
    bool isPinned(void*);

    bool contains(void*, CopiedBlock*&);

    size_t totalMemoryAllocated() { return m_totalMemoryAllocated; }
    size_t totalMemoryUtilized() { return m_totalMemoryUtilized; }

    CopiedBlock* blockFor(void*);

private:
    CheckedBoolean tryAllocateSlowCase(size_t, void**);
    CheckedBoolean addNewBlock();
    CheckedBoolean allocateNewBlock(CopiedBlock**);
    bool fitsInCurrentBlock(size_t);
    
    void* allocateFromBlock(CopiedBlock*, size_t);
    CheckedBoolean tryAllocateOversize(size_t, void**);
    CheckedBoolean tryReallocateOversize(void**, size_t, size_t);
    
    bool isOversize(size_t);
    
    CheckedBoolean borrowBlock(CopiedBlock**);
    CheckedBoolean getFreshBlock(AllocationEffort, CopiedBlock**);
    void doneFillingBlock(CopiedBlock*);
    void recycleBlock(CopiedBlock*);
    bool fitsInBlock(CopiedBlock*, size_t);
    CopiedBlock* oversizeBlockFor(void* ptr);

    Heap* m_heap;

    CopiedBlock* m_currentBlock;

    TinyBloomFilter m_toSpaceFilter;
    TinyBloomFilter m_oversizeFilter;
    HashSet<CopiedBlock*> m_toSpaceSet;

    Mutex m_toSpaceLock;
    Mutex m_memoryStatsLock;

    DoublyLinkedList<HeapBlock>* m_toSpace;
    DoublyLinkedList<HeapBlock>* m_fromSpace;
    
    DoublyLinkedList<HeapBlock> m_blocks1;
    DoublyLinkedList<HeapBlock> m_blocks2;
    DoublyLinkedList<HeapBlock> m_oversizeBlocks;
   
    size_t m_totalMemoryAllocated;
    size_t m_totalMemoryUtilized;

    bool m_inCopyingPhase;

    Mutex m_loanedBlocksLock; 
    ThreadCondition m_loanedBlocksCondition;
    size_t m_numberOfLoanedBlocks;

    size_t m_pageSize;
    size_t m_pageMask;
    size_t m_maxAllocationSize;
    size_t m_blockSize;
    size_t m_blockMask;
    static const size_t s_initialBlockNum = 16;
};

} // namespace JSC

#endif
