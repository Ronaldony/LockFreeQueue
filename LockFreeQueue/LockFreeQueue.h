#ifndef __LOCKFREE_QUEUE_H__ 
#define __LOCKFREE_QUEUE_H__ 

#include "LockfreeMemoryPool.h"

#define dfLOCKFREE_QUEUE_MASKING_ADDR		0xFFFFFFFFFFFF
#define dfLOCKFREE_QUEUE_MASKING_COUNT		0xFFFF000000000000
#define dfLOCKFREE_QUEUE_MASKING_COUNT_ADD	0x0001000000000000
#define dfLOCKFREE_QUEUE_DEBUG_MAX  		0x7FFF

template <typename T>
class CLockFreeQueue
 {
 public:
    struct Node
    {
        T data;
        Node* next;
    };

 private:
     CLockFreeMemoryPool<Node>  _MemmoryPool;
     LONG64 alignas(64) _size;
     Node* alignas(64) _head;        // 시작노드를 포인트한다.
     Node* alignas(64) _tail;        // 마지막노드를 포인트한다.

 public:
     CLockFreeQueue()
     {
         _size = 0;
         _head = _MemmoryPool.Alloc();
         _head->next = NULL;
         _tail = _head;
     }

     LONG64 GetUseSize() { return _size; }
     LONG64 GetPoolSize() { return _MemmoryPool.GetPoolSize(); }

     inline static LONG64		GetTotalCapacityCount(void) { return CLockFreeMemoryPool<Node>::GetTotalCapacityCount(); }
     inline static LONG64		GetTotalPoolSize(void) { return CLockFreeMemoryPool<Node>::GetTotalPoolSize(); }
 
     void Enqueue(T data)
     {
         Node* pNode = _MemmoryPool.Alloc();

         pNode->data = data;
         pNode->next = NULL;

         while(true)
         {
             // ABA 문제를 고려하여 64bit 에서의 주소 사용 비트를 마스킹하여 Tail과 next 얻어온다.
             Node* pTempTail = _tail;
             Node* tail = (Node*)((ULONG_PTR)pTempTail & dfLOCKFREE_QUEUE_MASKING_ADDR);
             Node* next = tail->next;

             // 아래 수행 시간이 긴 Interlocked 명령을 최대한 수행하지 않기 위한 조건문
             if (NULL == next)
             {
                 // 1번 동작: tail의 next에 새로운 노드를 꽂는 동작
                 if (NULL == InterlockedCompareExchangePointer((PVOID*)&tail->next, pNode, next))
                 {
                     Node* pNewNode = (Node*)((((ULONG_PTR)pTempTail + dfLOCKFREE_QUEUE_MASKING_COUNT_ADD) & dfLOCKFREE_QUEUE_MASKING_COUNT) | (ULONG_PTR)(pNode));

                     // 2번 동작: tail을 새로운 tail로 변경하는 동작
                     InterlockedCompareExchangePointer((PVOID*)&_tail, pNewNode, pTempTail);
                     break;
                 }
             }
             else
             {
                Node* pNewNode = (Node*)((((ULONG_PTR)pTempTail + dfLOCKFREE_QUEUE_MASKING_COUNT_ADD) & dfLOCKFREE_QUEUE_MASKING_COUNT) | (ULONG_PTR)(next));
                InterlockedCompareExchangePointer((PVOID*)&_tail, pNewNode, pTempTail);
             }
         }

         InterlockedExchangeAdd64(&_size, 1);
     }
 
     int Dequeue(T& data)
     {
         while(true)
         {
             Node* tempHead = _head;
             Node* head = (Node*)((ULONG_PTR)tempHead & dfLOCKFREE_QUEUE_MASKING_ADDR);
             Node* next = head->next;

             if (next == NULL)
             {
                 // 큐가 비어있는 상태
                 return 0;
             }
             else
             {
                 Node* tail = (Node*)((ULONG_PTR)_tail & dfLOCKFREE_QUEUE_MASKING_ADDR);
                 if (tail == head)
                 {
                     // Head와 Tail이 같을 때 Tail을 Tail->next로 변경하여 Head가 Tail을 역전하는 상황을 막는다.
                     Node* tempTailNext = (Node*)((((ULONG_PTR)_tail + dfLOCKFREE_QUEUE_MASKING_COUNT_ADD) & dfLOCKFREE_QUEUE_MASKING_COUNT) | (ULONG_PTR)(tail->next));
                     if (tempHead != InterlockedCompareExchangePointer((PVOID*)&_tail, tempTailNext, tempHead))
                         continue;
                 }

                 // Head를 교체하기 전에 data를 미리 저장해두지 않고 Head 교체 후 data를 저장하게 되면
                 // 다른 스레드에서 next 노드를 대상으로 DeQ, EnQ 하는 상황이 되었을 때 next->data가 중복 DeQ 될 수 있다.
                 data = next->data;

                 Node* pNewHead = (Node*)((((ULONG_PTR)tempHead + dfLOCKFREE_QUEUE_MASKING_COUNT_ADD) & dfLOCKFREE_QUEUE_MASKING_COUNT) | (ULONG_PTR)(next));

                 if (tempHead == InterlockedCompareExchangePointer((PVOID*)&_head, pNewHead, tempHead))
                 {
                     _MemmoryPool.Free(head);
                     break;
                 }
             }
         }
         InterlockedExchangeAdd64(& _size, -1);
         return 1;
     }
 };

#endif
