/*
 *
 * (C) 2013-20 - ntop.org
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the ho2pe that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "ntop_includes.h"

/* ************************************ */

GenericHash::GenericHash(NetworkInterface *_iface, u_int _num_hashes,
			 u_int _max_hash_size, const char *_name) {
  num_hashes = _num_hashes;
  current_size = 0;
  /* Allow the total number of entries (that is, active and those idle but still not yet purged)
     to be 30% more than the maximum hash table size specified. This prevents memory from growing
     indefinitely when for example the purging is slow. */
  max_hash_size = _max_hash_size * 1.3;
  last_entry_id = 0;
  purge_step = max_val(num_hashes / PURGE_FRACTION, 1);
  walk_idle_start_hash_id = 0;
  name = strdup(_name ? _name : "???");
  memset(&entry_state_transition_counters, 0, sizeof(entry_state_transition_counters));

  iface = _iface;
  idle_entries = idle_entries_shadow = NULL;

  table = new GenericHashEntry*[num_hashes];
  for(u_int i = 0; i < num_hashes; i++)
    table[i] = NULL;

  locks = new RwLock*[num_hashes];
  for(u_int i = 0; i < num_hashes; i++) locks[i] = new RwLock();

  last_purged_hash = _num_hashes - 1;
}

/* ************************************ */

GenericHash::~GenericHash() {
  cleanup();

  delete[] table;

  for(u_int i = 0; i < num_hashes; i++) delete(locks[i]);
  delete[] locks;
  free(name);
}

/* ************************************ */

void GenericHash::cleanup() {
  vector<GenericHashEntry*> **ghvs[] = {&idle_entries, &idle_entries_shadow};

  for(u_int i = 0; i < sizeof(ghvs) / sizeof(ghvs[0]); i++) {
    if(*ghvs[i]) {
      if(!(*ghvs[i])->empty()) {
	for(vector<GenericHashEntry*>::const_iterator it = (*ghvs[i])->begin(); it != (*ghvs[i])->end(); ++it) {
	  delete *it;
	}
      }

      delete *ghvs[i];
      *ghvs[i] = NULL;
    }
  }

  for(u_int i = 0; i < num_hashes; i++) {
    if(table[i] != NULL) {
      GenericHashEntry *head = table[i];

      while(head) {
	GenericHashEntry *next = head->next();

	delete(head);
	head = next;
      }

      table[i] = NULL;
    }
  }
  current_size = 0;
}

/* ************************************ */

bool GenericHash::add(GenericHashEntry *h, bool do_lock) {
  if(hasEmptyRoom()) {
    u_int32_t hash = (h->key() % num_hashes);//������Ŀȡhashid,���������Ǹ�key��---comment by rwp 20200311

    if(do_lock)
      locks[hash]->wrlock(__FILE__, __LINE__);

    h->set_hash_table(this);
    h->set_hash_entry_id(last_entry_id++);
    h->set_next(table[hash]);//��һ��ָ��ͷԪ��
    table[hash] = h;//��Ԫ�ر�ɡ�ͷ��
    current_size++;

    if(do_lock)
      locks[hash]->unlock(__FILE__, __LINE__);

    return(true);
  } else
    return(false);
}

/* ************************************ */

/* This method updates the hash entries state and purges idle entries. */
void GenericHash::walkAllStates(bool (*walker)(GenericHashEntry *h, void *user_data), void *user_data) {
  vector<GenericHashEntry*> *cur_idle = NULL;
  u_int new_walk_idle_start_hash_id = 0;
  bool update_walk_idle_start_hash_id;

  if(idle_entries) {
    cur_idle = idle_entries;
    idle_entries = NULL;
  }

  //������ڡ�����̬���б������֮���˺�������һ��---comment by rwp 20200307
  if(cur_idle) {
    if(!cur_idle->empty()) {
	  ntop->getTrace()->traceEvent(TRACE_ERROR, "@@@@@walkAllStates function clean the idle entitys, num:%d",cur_idle->size());
      for(vector<GenericHashEntry*>::const_iterator it = cur_idle->begin(); it != cur_idle->end(); ++it) {
	walker(*it, user_data);//���ûص���������ghs[i]->walkAllStates(generic_periodic_hash_entry_state_update, &periodic_ht_state_update_user_data); һ�����ڼ��١�����������
	ntop->getTrace()->traceEvent(TRACE_ERROR, "@@@@@walkAllStates delete hash[key:%u]", (*it)->key());
	delete *it;//ɾ����Ŀ---comment by rwp 20200307
	entry_state_transition_counters.num_purged++;
      }
    }
    delete cur_idle;
  }

  /*
    To implement fairness, the walkIdle starts from walk_idle_start_hash_id and not from zero.
    walk_idle_start_hash_id is updated on the basis of the return value of the walker function.
    walk_idle_start_hash_id is updated the FIRST time the walker function returns true.
    The walker function is supposed to start returning true when it's deadline is approaching, that
    is, when there's no more time left to fully perform all the necessary walker operations and only
    a limited, strictly necessary set of operations is performed.
    So basically walkIdle always visit all hash table entries but, as it starts from walk_idle_start_hash_id,
    it guarantees that all entries get an equal chance to have their walker operations fully performed.
  */
  u_int hash_id = walk_idle_start_hash_id;//��ƽ����

  do {
    if(table[hash_id]) {
      GenericHashEntry *head;

      locks[hash_id]->rdlock(__FILE__, __LINE__);

      head = table[hash_id];
      while(head) {
	GenericHashEntry *next = head->next();

	if(head->get_state() >= hash_entry_state_idle)
	  ntop->getTrace()->traceEvent(TRACE_ERROR, "Unexpected idle state found [%u]", head->get_state());

	if(!head->idle()) {
	  update_walk_idle_start_hash_id = walker(head, user_data);//ע������ص�ֻ������idle����Ŀ

	  /* Check if it is time to update the new start hash id */
	  if(update_walk_idle_start_hash_id && new_walk_idle_start_hash_id == 0)
	    new_walk_idle_start_hash_id = hash_id;
	}

	head = next;
      } /* while */

      locks[hash_id]->unlock(__FILE__, __LINE__);
    }

    hash_id = hash_id == num_hashes - 1 ? 0 /* Start over */ : hash_id + 1;
  } while(hash_id != walk_idle_start_hash_id);//ѭ������hash_id���൱��һ����Բ��Ͱ������---comment by rwp 20200319

  walk_idle_start_hash_id = new_walk_idle_start_hash_id;
}

/* ************************************ */

bool GenericHash::walk(u_int32_t *begin_slot,
		       bool walk_all,
		       bool (*walker)(GenericHashEntry *h, void *user_data, bool *entryMatched),
		       void *user_data) {
  bool found = false;
  u_int16_t tot_matched = 0;

  //������hash����ۡ�Key�еĶ����е��á��ص���ȡ�÷ǡ�idel��״̬��ֵ�����زۺ�
  for(u_int hash_id = *begin_slot; hash_id < num_hashes; hash_id++) {//begin_slot����ָ��hash�в��ҡ���ʼ��ۡ�---comment by rwp 20200311
    if(table[hash_id] != NULL) {
      GenericHashEntry *head;

#if WALK_DEBUG
      ntop->getTrace()->traceEvent(TRACE_NORMAL, "[walk] Locking %d [%p]", hash_id, locks[hash_id]);
#endif

      locks[hash_id]->rdlock(__FILE__, __LINE__);
      head = table[hash_id];

      while(head) {
	GenericHashEntry *next = head->next();

        /* FIXX get_state() does not always match idle() as the latter can be 
         * overriden (e.g. Flow), leading to wolking entries that are actually
         * idle even with walk_idle = false, what about using idle() here? */

	if(!head->idle()) {
	  bool matched = false;
	  bool rc = walker(head, user_data, &matched);//���ûص���������---comment by rwp 20200311

	  if(matched) tot_matched++;

	  if(rc) {
	    found = true;
	    break;
	  }
	}

	head = next;
      } /* while */

      locks[hash_id]->unlock(__FILE__, __LINE__);
      // ntop->getTrace()->traceEvent(TRACE_NORMAL, "[walk] Unlocked %d", hash_id);

      if((tot_matched >= MIN_NUM_HASH_WALK_ELEMS) /* At least a few entries have been returned */
	 && (!walk_all)) {
	u_int32_t next_slot  = (hash_id == (num_hashes-1)) ? 0 /* start over */ : (hash_id+1);

	*begin_slot = next_slot;
#if WALK_DEBUG
	ntop->getTrace()->traceEvent(TRACE_NORMAL, "[walk] Over [nextSlot: %u][hash_id: %u][tot_matched: %u]",
				     next_slot, hash_id, tot_matched);
#endif

	return(found);
      }

      if(found)
	break;
    }
  }

  if(!found)
    *begin_slot = 0 /* start over */;

#if WALK_DEBUG
  ntop->getTrace()->traceEvent(TRACE_NORMAL, "[walk] Over [tot_matched: %u]", tot_matched);
#endif

  return(found);
}

/* ************************************ */

/*
  Bucket Lifecycle

  Active -> Idle -> Ready to be Purged -> Purged
*/

u_int GenericHash::purgeIdle(bool force_idle) {
  u_int i, num_detached = 0, buckets_checked = 0;
  time_t now = time(NULL);
  /* Visit all entries when force_idle is true */
  u_int visit_fraction = !force_idle ? purge_step : num_hashes;
  ssize_t idle_entries_shadow_old_size;
  vector<GenericHashEntry*>::const_iterator it;

  if(!idle_entries) {
    idle_entries = idle_entries_shadow;
    try {
      idle_entries_shadow = new vector<GenericHashEntry*>;
    } catch(std::bad_alloc& ba) {
      ntop->getTrace()->traceEvent(TRACE_ERROR, "Memory allocation error");
      return 0;
    }
  }

  idle_entries_shadow_old_size = idle_entries_shadow->size();

#if WALK_DEBUG
  ntop->getTrace()->traceEvent(TRACE_NORMAL, "[%s @ %s] Begin purgeIdle() [begin index: %u][purge step: %u][size: %u][force_idle: %u]",
			       name, iface->get_name(), last_purged_hash, visit_fraction, getNumEntries(), force_idle ? 1 : 0);
#endif

  /* Visit at least MIN_NUM_VISITED_ENTRIES entries at each iteration regardless of the hash size */
  u_int j;
  
  for(j = 0; j < num_hashes; j++) {
    /*
      Initially visit the visit_fraction of the hash, but it we have
      visited too few elements we keep visiting until a minimum number
      of entries is reached
    */
    if((j > visit_fraction) && (buckets_checked > MIN_NUM_VISITED_ENTRIES))
      break;
    
    if(++last_purged_hash == num_hashes) last_purged_hash = 0;
    i = last_purged_hash;

    if(table[i] != NULL) {
      GenericHashEntry *head, *prev = NULL;

      // ntop->getTrace()->traceEvent(TRACE_NORMAL, "[purge] Locking hash_id:%d", i);
      if(!locks[i]->trywrlock(__FILE__, __LINE__))
	continue; /* Busy, will retry next round */

      head = table[i];

      while(head) {
	HashEntryState head_state = head->get_state();
	GenericHashEntry *next = head->next();

	buckets_checked++;

	switch(head_state) {	  
	case hash_entry_state_idle:
	  /* As an idle entry is always removed immediately from the hash table
	     This walk should never find any such entry */
	  ntop->getTrace()->traceEvent(TRACE_ERROR, "Unexpected state found [%u]", head_state);
	  break;

	case hash_entry_state_allocated:
	  /* TCP flows with 3WH not yet completed (or collected with no TCP flags) fall here */
	  /* Don't break */
	case hash_entry_state_flow_notyetdetected:
	  /* UDP flows or TCP flows for which the 3WH is completed but protocol hasn't been detected yet */
	  head->housekeep(now);
	  /* Don't break  */
	case hash_entry_state_flow_protocoldetected:
	  /* Once the protocol is detected, there's no need to housekeep */
	  if(force_idle) goto detach_idle_hash_entry;
	  break;

	case hash_entry_state_active:
	  if(force_idle
	     || (head->is_hash_entry_state_idle_transition_possible()
			 && head->is_hash_entry_state_idle_transition_ready())) {
		  ntop->getTrace()->traceEvent(TRACE_NORMAL, "active state's hash[key:%u] ready to set idle", head->key());
	  detach_idle_hash_entry:
	    idle_entries_shadow->push_back(head);

	    if(!prev)
	      table[i] = next;
	    else
	      prev->set_next(next);

	    num_detached++, current_size--;
	    head = next;
	    continue;
	  }
	  break;
	}

	prev = head;
	head = next;
      } /* while */

      locks[i]->unlock(__FILE__, __LINE__);
      // ntop->getTrace()->traceEvent(TRACE_NORMAL, "[purge] Unlocked %d", i);
    }
  }

#ifdef WALK_DEBUG
  ntop->getTrace()->traceEvent(TRACE_NORMAL, "[%s][current_size: %u][visit_fraction: %u/%u (visited %u)][buckets_checked: %u]",
			       name, current_size, visit_fraction, num_hashes, j, buckets_checked);
#endif
  
  /* Actual idling can be performed when the hash table is no longer locked. */
  if(num_detached) {
    it = idle_entries_shadow->begin();
    advance(it, idle_entries_shadow_old_size);

    for(; it != idle_entries_shadow->end(); it++) {
      (*it)->set_hash_entry_state_idle();//�˴�������������ʵ�֣��������м���������decHostNum��---comment by rwp 20200307
      entry_state_transition_counters.num_idle_transitions++;
    }
  }

#if WALK_DEBUG
  if(/* (num_detached > 0) && */ (!strcmp(name, "FlowHash")))
    ntop->getTrace()->traceEvent(TRACE_NORMAL,
				 "[%s @ %s] purgeIdle() [num_detached: %u][num_checked: %u][end index: %u][current_size: %u]",
				 name, iface->get_name(), num_detached, buckets_checked, last_purged_hash, current_size);
#endif

  return(num_detached);
}

/* ************************************ */

int32_t GenericHash::getNumIdleEntries() const {
  return entry_state_transition_counters.num_idle_transitions - entry_state_transition_counters.num_purged;
};

/* ************************************ */

bool GenericHash::hasEmptyRoom() {
  return getNumEntries() + getNumIdleEntries() <= max_hash_size;
};

/* ************************************ */

void GenericHash::lua(lua_State *vm) {
  int64_t num_idle;

  lua_newtable(vm);

  lua_push_uint64_table_entry(vm, "max_hash_size", (u_int64_t)max_hash_size);

  /* Hash Entry states */
  lua_newtable(vm);

#if 0
  ntop->getTrace()->traceEvent(TRACE_NORMAL, "[%s] [total idle: %u][tot purged: %u]",
			       name,
			       entry_state_transition_counters.num_idle_transitions,
			       entry_state_transition_counters.num_purged);
#endif

  num_idle = getNumIdleEntries();
  if(num_idle < 0)
    ntop->getTrace()->traceEvent(TRACE_ERROR, "Internal error: unexpected number of entries in state [iface: %s][%s][hash_entry_state_idle: %i][num_idle_transitions: %u][num_purged: %u]", iface ? iface->get_name(): "", name, num_idle, entry_state_transition_counters.num_idle_transitions, entry_state_transition_counters.num_purged);
  else
    lua_push_uint64_table_entry(vm, "hash_entry_state_idle", (u_int64_t)num_idle);

  lua_push_uint64_table_entry(vm, "hash_entry_state_active", (u_int64_t)getNumEntries());

  lua_pushstring(vm, "hash_entry_states");
  lua_insert(vm, -2);
  lua_settable(vm, -3);

  lua_pushstring(vm, name ? name : "");
  lua_insert(vm, -2);
  lua_settable(vm, -3);
}
