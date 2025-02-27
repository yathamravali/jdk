/*
 * Copyright (c) 2017, 2023, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "gc/serial/defNewGeneration.inline.hpp"
#include "gc/serial/serialHeap.hpp"
#include "gc/serial/tenuredGeneration.inline.hpp"
#include "gc/shared/gcLocker.inline.hpp"
#include "gc/shared/genMemoryPools.hpp"
#include "gc/shared/strongRootsScope.hpp"
#include "gc/shared/suspendibleThreadSet.hpp"
#include "memory/universe.hpp"
#include "runtime/mutexLocker.hpp"
#include "services/memoryManager.hpp"

SerialHeap* SerialHeap::heap() {
  return named_heap<SerialHeap>(CollectedHeap::Serial);
}

SerialHeap::SerialHeap() :
    GenCollectedHeap(Generation::DefNew,
                     Generation::MarkSweepCompact,
                     "Copy:MSC"),
    _eden_pool(nullptr),
    _survivor_pool(nullptr),
    _old_pool(nullptr) {
  _young_manager = new GCMemoryManager("Copy", "end of minor GC");
  _old_manager = new GCMemoryManager("MarkSweepCompact", "end of major GC");
}

void SerialHeap::initialize_serviceability() {

  DefNewGeneration* young = young_gen();

  // Add a memory pool for each space and young gen doesn't
  // support low memory detection as it is expected to get filled up.
  _eden_pool = new ContiguousSpacePool(young->eden(),
                                       "Eden Space",
                                       young->max_eden_size(),
                                       false /* support_usage_threshold */);
  _survivor_pool = new SurvivorContiguousSpacePool(young,
                                                   "Survivor Space",
                                                   young->max_survivor_size(),
                                                   false /* support_usage_threshold */);
  TenuredGeneration* old = old_gen();
  _old_pool = new GenerationPool(old, "Tenured Gen", true);

  _young_manager->add_pool(_eden_pool);
  _young_manager->add_pool(_survivor_pool);
  young->set_gc_manager(_young_manager);

  _old_manager->add_pool(_eden_pool);
  _old_manager->add_pool(_survivor_pool);
  _old_manager->add_pool(_old_pool);
  old->set_gc_manager(_old_manager);

}

GrowableArray<GCMemoryManager*> SerialHeap::memory_managers() {
  GrowableArray<GCMemoryManager*> memory_managers(2);
  memory_managers.append(_young_manager);
  memory_managers.append(_old_manager);
  return memory_managers;
}

GrowableArray<MemoryPool*> SerialHeap::memory_pools() {
  GrowableArray<MemoryPool*> memory_pools(3);
  memory_pools.append(_eden_pool);
  memory_pools.append(_survivor_pool);
  memory_pools.append(_old_pool);
  return memory_pools;
}

void SerialHeap::young_process_roots(OopIterateClosure* root_closure,
                                     OopIterateClosure* old_gen_closure,
                                     CLDClosure* cld_closure) {
  MarkingCodeBlobClosure mark_code_closure(root_closure, CodeBlobToOopClosure::FixRelocations, false /* keepalive nmethods */);

  process_roots(SO_ScavengeCodeCache, root_closure,
                cld_closure, cld_closure, &mark_code_closure);

  old_gen()->younger_refs_iterate(old_gen_closure);
}

void SerialHeap::safepoint_synchronize_begin() {
  if (UseStringDeduplication) {
    SuspendibleThreadSet::synchronize();
  }
}

void SerialHeap::safepoint_synchronize_end() {
  if (UseStringDeduplication) {
    SuspendibleThreadSet::desynchronize();
  }
}

HeapWord* SerialHeap::allocate_loaded_archive_space(size_t word_size) {
  MutexLocker ml(Heap_lock);
  return old_gen()->allocate(word_size, false /* is_tlab */);
}

void SerialHeap::complete_loaded_archive_space(MemRegion archive_space) {
  assert(old_gen()->used_region().contains(archive_space), "Archive space not contained in old gen");
  old_gen()->complete_loaded_archive_space(archive_space);
}

void SerialHeap::pin_object(JavaThread* thread, oop obj) {
  GCLocker::lock_critical(thread);
}

void SerialHeap::unpin_object(JavaThread* thread, oop obj) {
  GCLocker::unlock_critical(thread);
}
