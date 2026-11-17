#ifndef EXECUTION_CONTROLLER_H
#define EXECUTION_CONTROLLER_H

// Both return false if a task could not run or be checked (a problem has been
// reported); EF5 then exits with status 1.
bool ExecuteTasks();
bool ExecuteChecks();

#endif
