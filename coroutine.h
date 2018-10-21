#ifndef C_COROUTINE_H
#define C_COROUTINE_H

#define COROUTINE_DEAD 0
#define COROUTINE_READY 1
#define COROUTINE_RUNNING 2
#define COROUTINE_SUSPEND 3

struct schedule;  // ǰ������Э�̵�����struct schedule����

typedef void (*coroutine_func)(struct schedule *, void *ud); // ����һ������ָ������

struct schedule * coroutine_open(void);   // ����Э�̵�����
void coroutine_close(struct schedule *);  // �ر�Э�̵�����

int coroutine_new(struct schedule *, coroutine_func, void *ud);   // ����Э������,��������������
void coroutine_resume(struct schedule *, int id);                 // �ָ�Э�̺�Ϊid��Э������
int coroutine_status(struct schedule *, int id);                  // ����Э������id����Э�̵ĵ�ǰ״̬
int coroutine_running(struct schedule *);                         // ���ص�����S������running��Э������id
void coroutine_yield(struct schedule *);                          // ���浱ǰ�����ĺ��жϵ�ǰЭ�̵�ִ��

#endif
