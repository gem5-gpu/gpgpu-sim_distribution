#include "gpu-cache_gem5.h"

l1icache_gem5::l1icache_gem5(gpgpu_t* _gpu, const char *name, cache_config &config,
        int core_id, int type_id, mem_fetch_interface *memport,
        enum mem_fetch_status status)
    : read_only_cache(name, config, core_id, type_id, memport, status),
      abstractGPU(_gpu), shaderCore(NULL)
{
    m_sid = core_id;
}

enum cache_request_status
l1icache_gem5::access(new_addr_type addr, mem_fetch *mf, unsigned time,
        std::list<cache_event> &events)
{
    assert( mf->get_data_size() <= m_config.get_line_sz());
    assert(m_config.m_write_policy == READ_ONLY);
    assert(!mf->get_is_write());
    new_addr_type block_addr = m_config.block_addr(addr);
    unsigned cache_index = (unsigned)-1;
    enum cache_request_status status = m_tag_array->probe(block_addr,cache_index);
    if ( status == HIT ) {
        m_tag_array->access(block_addr,time,cache_index); // update LRU state
        return HIT;
    }
    if ( status != RESERVATION_FAIL ) {
        bool mshr_hit = m_mshrs.probe(block_addr);
        bool mshr_avail = !m_mshrs.full(block_addr);
        if ( mshr_hit && mshr_avail ) {
            m_tag_array->access(addr,time,cache_index);
            m_mshrs.add(block_addr,mf);
            return MISS;
        } else if ( !mshr_hit && mshr_avail && (m_miss_queue.size() < m_config.m_miss_queue_size) ) {
            m_tag_array->access(addr,time,cache_index);
            m_mshrs.add(block_addr,mf);
            m_extra_mf_fields[mf] = extra_mf_fields(block_addr,cache_index, mf->get_data_size());
            // @TODO: Can we move this so that it isn't executed each call?
            if (!shaderCore) {
                shaderCore = abstractGPU->gem5CudaGPU->getCudaCore(m_sid);
            }
            // Send access into Ruby through shader core
            shaderCore->icacheFetch((Addr)addr, mf);
            mf->set_data_size( m_config.get_line_sz() );
            mf->set_status(m_miss_queue_status,time);
            events.push_back(READ_REQUEST_SENT);
            return MISS;
        }
    }
    return RESERVATION_FAIL;
}

void l1icache_gem5::cycle()
{
    // Intentionally left empty
}
