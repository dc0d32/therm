#ifndef __TASKS_H_
#define __TASKS_H_

#include <arduino.h>

#define get_ts millis

class scheduler
{

    struct task_t
    {
        void *func_ptr;
        int id;
        void *params;
        int priority;

        unsigned long next_ts;
        unsigned int period;
    };

    task_t *tasks;
    int num_tasks = 0, max_tasks;

    int find_task(void *func, int id);

    void update_task(int task_idx, void *params, int priority, unsigned int period, unsigned int start_delay);

    void rearm_task(int task_idx);

    bool remove_task(int task_idx);

public:
    scheduler(int max_tasks);

    ~scheduler();

    bool add_or_update_task(void *func, int id, void *params, int priority, unsigned int period, unsigned int start_delay);

    bool remove_task(void *func, int id);

    void run(int notask_delay);
};

#endif
