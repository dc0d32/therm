#include "tasks.h"

scheduler sched(MAX_NUM_TASKS); // 'task' scheduler

int scheduler::find_task(void *func, int id)
{
    for (int i = 0; i < num_tasks; i++)
    {
        if (tasks[i].id == id && tasks[i].func_ptr == func)
        {
            return i;
        }
    }
    return -1;
}

void scheduler::update_task(int task_idx, void *params, int priority, unsigned int period, unsigned int start_delay)
{
    tasks[task_idx].params = params;
    tasks[task_idx].period = period;
    tasks[task_idx].priority = priority;
    tasks[task_idx].next_ts = get_ts() + start_delay;

    // put the task in correct priority order
    // other tasks are already sorted

    int direction_to_move = -1;
    if ((task_idx == 0) || (tasks[task_idx - 1].priority <= priority))
        direction_to_move = 0;

    if (direction_to_move == 0)
    {
        if (task_idx < num_tasks - 1 && tasks[task_idx + 1].priority < priority)
            direction_to_move = 1;
    }

    if (direction_to_move != 0)
    {
        // move to correct place
        int target_position = -1;
        int other_task_idx = task_idx + direction_to_move;
        for (; other_task_idx >= 0 && other_task_idx < num_tasks; other_task_idx += direction_to_move)
        {
            if ((tasks[other_task_idx].priority - priority) * direction_to_move >= 0)
            {
                target_position = other_task_idx - direction_to_move;
                break;
            }
        }
        if (other_task_idx < 0)
        {
            target_position = 0;
        }
        else if (other_task_idx == num_tasks)
        {
            target_position = num_tasks - 1;
        }

        task_t temp = tasks[task_idx];
        if (direction_to_move > 0)
        {
            memmove(&tasks[task_idx], &tasks[task_idx + 1], (target_position - task_idx) * sizeof(task_t));
        }
        else
        {
            memmove(&tasks[target_position + 1], &tasks[target_position], (task_idx - target_position) * sizeof(task_t));
        }
        tasks[target_position] = temp;
    }
}

void scheduler::rearm_task(int task_idx)
{
    tasks[task_idx].next_ts = get_ts() + tasks[task_idx].period;
}

bool scheduler::remove_task(int task_idx)
{
    if (task_idx >= 0 && task_idx < num_tasks)
    {
        memmove(&tasks[task_idx], &tasks[task_idx + 1], (num_tasks - task_idx - 1) * sizeof(task_t));
        --num_tasks;
        return true;
    }
    return false;
}

scheduler::scheduler(int max_tasks)
{
    this->max_tasks = max_tasks;
    tasks = new task_t[max_tasks];
    num_tasks = 0;
}

scheduler::~scheduler()
{
    if (tasks != NULL)
    {
        delete[] tasks;
    }
}

bool scheduler::add_or_update_task(void *func, int id, void *params, int priority, unsigned int period, unsigned int start_delay)
{
    if (num_tasks >= max_tasks)
        return false;

    noInterrupts();

    int task_idx = find_task(func, id);

    if (task_idx < 0)
    {
        // new task
        task_idx = num_tasks++;

        tasks[task_idx].func_ptr = func;
        tasks[task_idx].id = id;
    }

    update_task(task_idx, params, priority, period, start_delay);

    // Serial.printf("created task %d\n", task_idx);

    interrupts();

    return true;
}

bool scheduler::remove_task(void *func, int id)
{
    if (num_tasks == 0)
        return false;
    noInterrupts();

    int task_idx = find_task(func, id);

    bool success = remove_task(task_idx);

    // Serial.printf("removed task %d (%d)\n", task_idx, success);

    interrupts();

    return success;
}

void scheduler::run(int notask_delay)
{
    unsigned long ts = get_ts();
    // Serial.printf("(%d) check run, total = %d\n", ts, num_tasks);
    int num_tasks_run = 0;
    for (int task_idx = 0; task_idx < num_tasks; task_idx++)
    {
        auto task = tasks[task_idx];
        if (task.next_ts <= ts)
        {
            // run the task
            // Serial.printf("run task %d \n", task_idx);
            ((void (*)(void *))task.func_ptr)(task.params);

            // after the function runs, it could have removed itself from the schedule or updated timings -- check for that
            // even if the task updates itself, it can end up at the same idx; but it would have a different next_ts
            auto task2 = tasks[task_idx];
            if (task.func_ptr == task2.func_ptr && task.id == task2.id && task.next_ts == task2.next_ts)
            {
                // task did not move or update
                if (task.period > 0)
                {
                    rearm_task(task_idx);
                    // Serial.printf("rearm task %d \n", task_idx);
                }
                else
                {
                    // task done, no repeat. delete
                    remove_task(task_idx);
                    // Serial.printf("delete task %d \n", task_idx);
                }
            }
            yield(); // give network stack a chance to grab stuff from wifi
            ++num_tasks_run;
        }
    }
    if (num_tasks_run == 0)
    {
        delay(notask_delay);
    }
}
