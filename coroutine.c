#include "coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

#if __APPLE__ && __MACH__
	#include <sys/ucontext.h>
#else 
	#include <ucontext.h>
#endif 

#define STACK_SIZE (1024*1024)
#define DEFAULT_COROUTINE 16

struct coroutine;

// Э�̵�����
struct schedule {
	char stack[STACK_SIZE];
	ucontext_t main;        // ����running��Э����ִ��������л����������ģ������ǷǶԳ�Э�̣����Ը������������ӹ�Э�̽�����ĳ������Ȩ
	int nco;                // ���������ѱ����Э������
	int cap;                // ��������Э�̵��������
	int running;            // ������������running��Э��id
	struct coroutine **co;  // �����ڴ�ռ䣬���ڴ洢����Э������
};

// Э����������
struct coroutine {
	coroutine_func func;    // Э�̺���
	void *ud;               // Э�̺����Ĳ���(�û�����)
	ucontext_t ctx;         // Э��������
	struct schedule * sch;  // Э�������ĵ�����
	// ptrdiff_t������stddef.h(cstddef)�У�ͨ��������Ϊlong int���ͣ�ͨ��������������ָ����������Ľ��.
	ptrdiff_t cap;          // Э��ջ���������
	ptrdiff_t size;         // Э��ջ�ĵ�ǰ����
	int status;             // Э��״̬(COROUTINE_DEAD/COROUTINE_READY/COROUTINE_RUNNING/COROUTINE_SUSPEND)
	char *stack;            // Э��ջ
};

// ����Э������(�����ڴ�ռ�)����ʼ��
struct coroutine * 
_co_new(struct schedule *S , coroutine_func func, void *ud) {
	struct coroutine * co = malloc(sizeof(*co));
	co->func = func;                // ��ʼ��Э�̺���
	co->ud = ud;                    // ��ʼ���û�����
	co->sch = S;                    // ��ʼ��Э�������ĵ�����
	co->cap = 0;                    // ��ʼ��Э��ջ���������
	co->size = 0;                   // ��ʼ��Э��ջ�ĵ�ǰ����
	co->status = COROUTINE_READY;   // ��ʼ��Э��״̬
	co->stack = NULL;               // ��ʼ��Э��ջ
	return co;
}

// ����Э������(�ͷ��ڴ�ռ�)
void
_co_delete(struct coroutine *co) {
	free(co->stack);
	free(co);
}

// ����Э�̵�����schedule
struct schedule * 
coroutine_open(void) {
	struct schedule *S = malloc(sizeof(*S));              // �Ӷ���Ϊ�����������ڴ�ռ�
	S->nco = 0;                                           // ��ʼ���������ĵ�ǰЭ������
	S->cap = DEFAULT_COROUTINE;                           // ��ʼ�������������Э������
	S->running = -1;
	S->co = malloc(sizeof(struct coroutine *) * S->cap);  // Ϊ�������е�Э�̷���洢�ռ�
	memset(S->co, 0, sizeof(struct coroutine *) * S->cap);
	return S;
}

// ����Э�̵�����schedule
void 
coroutine_close(struct schedule *S) {
	int i;
	for (i=0;i<S->cap;i++) {
		struct coroutine * co = S->co[i];
		if (co) {
			_co_delete(co);
		}
	}
	free(S->co);
	S->co = NULL;
	free(S);
}

// ����Э�����񡢲���������������
int 
coroutine_new(struct schedule *S, coroutine_func func, void *ud) {
	// ����Э������(�����ڴ�ռ�)����ʼ��
	struct coroutine *co = _co_new(S, func , ud);
	
	// ��Э������co���������S,�����ظ�Э�������id
	if (S->nco >= S->cap) {
		// ����������S��Э�̵��������,Ȼ��Э������co���������S,�����ظ�Э�������id
		int id = S->cap;
		S->co = realloc(S->co, S->cap * 2 * sizeof(struct coroutine *));
		memset(S->co + S->cap , 0 , sizeof(struct coroutine *) * S->cap);
		S->co[S->cap] = co;
		S->cap *= 2;
		++S->nco;
		return id;
	} else {
		// ��Э������co���������S,�����ظ�Э�������id
		int i;
		for (i=0;i<S->cap;i++) {
			int id = (i+S->nco) % S->cap;
			if (S->co[id] == NULL) {
				S->co[id] = co;
				++S->nco;
				return id;
			}
		}
	}
	assert(0);
	return -1;
}

// ������Э�̵�һ��ִ��ʱ����ں���(����ִ��Э��,�������ƺ�����)
static void
mainfunc(uint32_t low32, uint32_t hi32) {
	uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hi32 << 32);
	struct schedule *S = (struct schedule *)ptr;
	int id = S->running;
	struct coroutine *C = S->co[id];
	C->func(S,C->ud);
	_co_delete(C);
	S->co[id] = NULL;
	--S->nco;
	S->running = -1;
}

// �ָ�Э�̺�Ϊid��Э������
void 
coroutine_resume(struct schedule * S, int id) {
	assert(S->running == -1);
	assert(id >=0 && id < S->cap);
	struct coroutine *C = S->co[id];
	if (C == NULL)
		return;
	int status = C->status;
	switch(status) {
	case COROUTINE_READY:
		getcontext(&C->ctx);                    // ��ȡ����ǰ������
		C->ctx.uc_stack.ss_sp = S->stack;       // ����������C->ctx��ջ
		C->ctx.uc_stack.ss_size = STACK_SIZE;   // ����������C->ctx��ջ����
		C->ctx.uc_link = &S->main;              // ����������C->ctxִ�����ָ���S->main������, ����ǰ�߳���û�������Ŀ�ִ�ж��˳�
		S->running = id;
		C->status = COROUTINE_RUNNING;
		uintptr_t ptr = (uintptr_t)S;
		//�Ͱ汾glibcʵ�����ⲻ֧��64bitָ�룬����ͨ��2��32λint����S��ָ��
		makecontext(&C->ctx, (void (*)(void)) mainfunc, 2, (uint32_t)ptr, (uint32_t)(ptr>>32)); // �޸�������C->ctx, �µ���������ִ�к���mainfunc
		swapcontext(&S->main, &C->ctx);         // ���ֵ�ǰ�����ĵ�S->main, �л���ǰ������ΪC->ctx
		break;
	case COROUTINE_SUSPEND:
		memcpy(S->stack + STACK_SIZE - C->size, C->stack, C->size);  // ����Э��ջC->stack��S->stack
		S->running = id;                       // ���õ�ǰ���е�Э��id
		C->status = COROUTINE_RUNNING;         // �޸�Э��C��״̬
		swapcontext(&S->main, &C->ctx);        // ���浱ǰ�����ĵ�S->main, �л���ǰ������ΪC->ctx
		break;
	default:
		assert(0);
	}
}

// ����Э��ջ
static void
_save_stack(struct coroutine *C, char *top) {
	char dummy = 0;
	assert(top - &dummy <= STACK_SIZE);
	if (C->cap < top - &dummy) {
		// ΪЭ��ջ�����ڴ�ռ�
		free(C->stack);
		C->cap = top-&dummy;
		C->stack = malloc(C->cap);
	}
	
	C->size = top - &dummy;
	memcpy(C->stack, &dummy, C->size); // dummyָ��Э�̵�ջ�ף�[top=>dummy]֮��Ϊʹ�õ�ջ�ռ䡣
}

// ���������ĺ��жϵ�ǰЭ�̵�ִ��,Ȼ���ɵ������е�main�����Ľӹܳ���ִ��Ȩ
void
coroutine_yield(struct schedule * S) {
	int id = S->running;
	assert(id >= 0);
	struct coroutine * C = S->co[id];
	assert((char *)&C > S->stack);
	_save_stack(C,S->stack + STACK_SIZE);   // ����Э��ջ
	C->status = COROUTINE_SUSPEND;          // �޸�Э��״̬
	S->running = -1;                        // �޸ĵ�ǰִ�е�Э��idΪ-1
	swapcontext(&C->ctx , &S->main);        // ���浱ǰЭ�̵������ĵ�C->ctx, �л���ǰ�����ĵ�S->main
}

// ����Э������id����Э�̵ĵ�ǰ״̬
int 
coroutine_status(struct schedule * S, int id) {
	assert(id>=0 && id < S->cap);
	if (S->co[id] == NULL) {
		return COROUTINE_DEAD;
	}
	return S->co[id]->status;
}

// ���ص�����S������running��Э������id
int 
coroutine_running(struct schedule * S) {
	return S->running;
}
