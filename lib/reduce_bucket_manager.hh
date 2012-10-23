#ifndef RBKTSMGR_H
#define RBKTSMGR_H

#include "mr-types.hh"
#include "comparator.hh"
#include "psrs.hh"

struct reduce_bucket_manager_base {
    virtual void init(int n) = 0;
    virtual void destroy() = 0;
    virtual void set_current_reduce_task(int i) = 0;
    virtual void merge_reduced_buckets(int ncpus, int lcpu) = 0;
};

template <typename T>
struct reduce_bucket_manager : public reduce_bucket_manager_base {
    static reduce_bucket_manager *instance() {
        static reduce_bucket_manager instance;
        return &instance;
    }
    void init(int n) {
        rb_.resize(n);
        for (int i = 0; i < n; ++i)
            rb_[i].init();
        assert(pthread_key_create(&current_task_key_, NULL) == 0);
        set_current_reduce_task(0);
    }
    void destroy() {
        rb_.resize(0);
    }
    typedef xarray<T> C;
    xarray<T> *get(int p) {
        return &rb_[p];
    }
    void emit(const T &p) {
        rb_[current_task()].push_back(p);
    }
    void set_current_reduce_task(int ir) {
        assert(size_t(ir) < rb_.size());
        assert(pthread_setspecific(current_task_key_,
                                   (void *)intptr_t(ir)) == 0);
    }
    /** @brief: merge the output buckets of reduce phase, i.e. the final output.
        For psrs, the result is stored in rb_[0]; for mergesort, the result are
        spread in rb[0..(ncpus - 1)]. */
    void merge_reduced_buckets(int ncpus, int lcpu) {
        C *out = NULL;
        const int use_psrs = USE_PSRS;
        if (!use_psrs) {
            out = mergesort(rb_, ncpus, lcpu,
                            comparator::final_output_pair_comp);
            shallow_free_subarray(rb_, lcpu, ncpus);
        } else {
            // only main cpu has output
            out = initialize_psrs<C>(lcpu, sum_subarray(rb_));
            psrs<C> *pi = psrs<C>::instance();
            assert(out || !pi->main_cpu(lcpu));
            pi->do_psrs(rb_, ncpus, lcpu, comparator::final_output_pair_comp);
            // Let one CPU free the input buckets
            if (pi->main_cpu(lcpu))
                shallow_free_subarray(rb_);
        }
        if (out) {
            rb_[lcpu].swap(*out);
            delete out;
        }
    }
    template <typename D>
    void transfer(int p, D *dst) {
        C *x = get(p);
        dst->data = x->array();
        dst->length = x->size();
        x->init();
    }
  private:
    int current_task() {
        return intptr_t(pthread_getspecific(current_task_key_));
    }
    reduce_bucket_manager() {}
    xarray<C> rb_; // reduce buckets
    pthread_key_t current_task_key_;
};

#endif