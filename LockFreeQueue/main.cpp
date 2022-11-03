#include <stdio.h>
#include <process.h>
#include <time.h>
#include <windows.h>
#include <new.h>
#include "LockFreeQueue.h"

#pragma comment(lib, "winmm.lib")

unsigned int __stdcall LockfreeQTest(void*);

#define dfTOTAL_QUEUE_SIZE		(dfPOOL_THREAD_NUM * dfALLOC_FREE_COUNT)
#define dfPOOL_THREAD_NUM	4
#define dfALLOC_FREE_COUNT	2

#define dfTOTAL_QUEUEING_LOOP_COUNT		1000000000

struct stData
{
	ULONG_PTR ullData;
	ULONG_PTR ullCount;
};

struct stDebugLog
{
	int			step;
	DWORD		threadID;
	ULONG_PTR	addr;
	ULONG_PTR	tryCnt;
	ULONG_PTR	cnt;
};

CLockFreeQueue<stData*> g_QueueObj;
ULONG64 alignas(64)g_CrashCount;
ULONG64 g_LoopCount[dfPOOL_THREAD_NUM];

void Crash(stData*);

int main()
{
	timeBeginPeriod(1);
	
	HANDLE* phThread;
	DWORD* pdwThreadID;

	printf("Start \n");

	// ������ ������ �� �°� EnQ�� DeQ �۾��ϱ� ���Ͽ� ��带 ���� �Ҵ�޾� �̸� ť���� �� ���´�.
	for (int cnt = 0; cnt < dfTOTAL_QUEUE_SIZE; cnt++)
	{
		stData* newData = new stData;
		newData->ullCount = 0;
		newData->ullData = 0;
		g_QueueObj.Enqueue(newData); 
	}

	// ť�� ������ ����
	phThread = new HANDLE[dfPOOL_THREAD_NUM];		
	pdwThreadID = new DWORD[dfPOOL_THREAD_NUM];

	for (int cnt = 0; cnt < dfPOOL_THREAD_NUM; cnt++)
	{
		phThread[cnt] = (HANDLE)_beginthreadex(NULL, 0, LockfreeQTest,
			(void*)cnt, CREATE_SUSPENDED, (unsigned int*)&pdwThreadID[cnt]);
	}

	// ť�� ������ �׽�Ʈ ����
	for (int cnt = 0; cnt < dfPOOL_THREAD_NUM; cnt++)
	{
		ResumeThread(phThread[cnt]);
	}

	//--------------------------------------------------------
	// ����͸�
	//--------------------------------------------------------
	LONG64 ldMonitoringCount = 0;
	LONG64 ldPrevTick = GetTickCount64();

	struct tm StartTime;
	struct tm NowTime;

	time_t timer = time(NULL);
	(void)localtime_s(&StartTime, &timer);

	while (1)
	{
		LONG64 ldNowTick = GetTickCount64();

		// 1�ʸ��� ����͸� ���� ������Ʈ
		if (abs(ldNowTick - ldPrevTick) > 1000)
		{
			struct tm NowTime;
			timer = time(NULL);
			(void)localtime_s(&NowTime, &timer);

			ldMonitoringCount++;
			printf("-----------------------------------------------\n");
			wprintf(L" Start Time: [%d/%d/%d %2d:%2d:%2d]\n", StartTime.tm_year + 1900, StartTime.tm_mon + 1, StartTime.tm_mday, StartTime.tm_hour,
				StartTime.tm_min, StartTime.tm_sec);
			wprintf(L" Now Time  : [%d/%d/%d %2d:%2d:%2d]\n", NowTime.tm_year + 1900, NowTime.tm_mon + 1, NowTime.tm_mday, NowTime.tm_hour, 
				NowTime.tm_min, NowTime.tm_sec);

			wprintf(L" Monitor Count: %lld\n\n", ldMonitoringCount);

			wprintf(L" Total EnQ Size: %d\n", dfTOTAL_QUEUE_SIZE);
			wprintf(L" Queue Use Size: %lld\n", g_QueueObj.GetUseSize());
			wprintf(L" Crash: %lld\n\n", g_CrashCount);
			
			wprintf(L" Thread Number: %d\n", dfPOOL_THREAD_NUM);
			wprintf(L" TOTAL Loop Count    : %d\n", dfTOTAL_QUEUEING_LOOP_COUNT);
			for (int cnt = 0; cnt < dfPOOL_THREAD_NUM; cnt++)
				wprintf(L" Thread %d Loop Count : %lld\n", cnt, g_LoopCount[cnt]);

			ldPrevTick = ldNowTick;
		}
		
		// ��� ������ ���� �� ������ ����
		if (WAIT_TIMEOUT != WaitForMultipleObjects(dfPOOL_THREAD_NUM, phThread, TRUE, 50))
		{
			printf("Success for exit all thread.\n");
			break;
		}
	}

	printf("-----------------------------------------------\n");
	printf("TEST CLEAR!!\n");
	printf("Queue Use Size: %lld\n", g_QueueObj.GetUseSize());
	printf("Crash: %lld\n\n", g_CrashCount);
	wprintf(L" TOTAL Loop Count    : %d\n", dfTOTAL_QUEUEING_LOOP_COUNT);
	for (int cnt = 0; cnt < dfPOOL_THREAD_NUM; cnt++)
		printf(" Thread %d Loop Count : %lld\n", cnt, g_LoopCount[cnt]);
	printf("-----------------------------------------------\n");

	timeEndPeriod(1);

	return 0;
}

//--------------------------------------------------------
// ť�� ������
// Prameter: (void*)������ ���� ���� ��ȣ
//--------------------------------------------------------
unsigned int __stdcall LockfreeQTest(void* param)
{
	stData* pstDataArr[dfALLOC_FREE_COUNT] = {0,};	// EnQ, DeQ ������
	LONG64 loopNum = (LONG64)param;					// ���� ī��Ʈ ���� �迭 ��ȣ

	//--------------------------------------------------------
	// ť�� �۾�
	// ���� Ƚ��(dfTOTAL_QUEUEING_LOOP_COUNT) ��ŭ ���� ����
	// * ���� ��� 1: ���� ���� ���� �� EnQ�� DeQ�� ������ ����ġ
	//--------------------------------------------------------
	while (g_LoopCount[loopNum] < dfTOTAL_QUEUEING_LOOP_COUNT)
	{
		g_LoopCount[loopNum]++;

		// Dequeue
		for (int cnt = 0; cnt < dfALLOC_FREE_COUNT; cnt++)
		{
			if (0 == g_QueueObj.Dequeue(pstDataArr[cnt]))
			{
				cnt--;
				continue;
			}

			// ������ ����
			pstDataArr[cnt]->ullData = 0;
			pstDataArr[cnt]->ullCount = 0;
		}


		// ���� ��� 2: �����͸� �˻��Ͽ� �ߺ� Dequeue ���� �˻�
		for (int cnt = 0; cnt < dfALLOC_FREE_COUNT; cnt++)
		{
			if (pstDataArr[cnt]->ullData != 0)
				Crash(pstDataArr[cnt]);
			if (pstDataArr[cnt]->ullCount != 0)
				Crash(pstDataArr[cnt]);
		}

		// ������ ����
		for (int cnt = 0; cnt < dfALLOC_FREE_COUNT; cnt++)
		{
			InterlockedIncrement(&pstDataArr[cnt]->ullData);
			InterlockedIncrement(&pstDataArr[cnt]->ullCount);
		}

		// Sleep(0)�� �ǵ�: �ִ��� �ð��� �������� Alloc�� Free�� ������Ű�� ���� ���
		Sleep(0);
		Sleep(0);
		Sleep(0);
		Sleep(0);
		Sleep(0);
		Sleep(0);
		Sleep(0);


		// ���� ��� 2: �����͸� �˻��Ͽ� �ߺ� Dequeue ���� �˻�
		for (int cnt = 0; cnt < dfALLOC_FREE_COUNT; cnt++)
		{
			if (pstDataArr[cnt]->ullData != 1)
				Crash(pstDataArr[cnt]);
			if (pstDataArr[cnt]->ullCount != 1)
				Crash(pstDataArr[cnt]);
		}

		// Enqueue
		for (int cnt = 0; cnt < dfALLOC_FREE_COUNT; cnt++)
		{
			g_QueueObj.Enqueue(pstDataArr[cnt]);
		}
	}
	
	return 0;
}

void Crash(stData* pStaData)
{
	InterlockedIncrement(&g_CrashCount);
	printf("thread id: %d / addr: %p\n", GetCurrentThreadId(), pStaData);
}