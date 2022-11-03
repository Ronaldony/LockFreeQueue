#ifndef  __LOCKFREE_MEMORY_POOL_H__
#define  __LOCKFREE_MEMORY_POOL_H__
//#include <Windows.h>

#define dfLOCKFREE_MEMORYPOOL_MASKING_ADDR		0xFFFFFFFFFFFF
#define dfLOCKFREE_MEMORYPOOL_MASKING_COUNT		0xFFFF000000000000
#define dfLOCKFREE_MEMORYPOOL_MASKING_COUNT_ADD	0x0001000000000000

#define dfLOCKFREE_MEMORYPOOL_BENCHMARKING

template <class DATA>
class CLockFreeMemoryPool
{
public:
	enum en_POOL
	{
		eSYSTEM_PAGE_SIZE = 4096,
		eALLOC_PAGE_RESERVE_SIZE = 65536
	};

	// 오브젝트 블럭
	struct alignas(64) st_BLOCK_NODE
	{
		LONG64 objectCode;						// 오브젝트 검사 코드
		DATA data;							// 오브젝트 - 실제적으로 사용자에게 넘겨줘야하는 주소
		LONG64 overflow;						// 노드 오버플로우 검사 용도
		st_BLOCK_NODE*			next;		// 다음 노드 포인터
	};

	//////////////////////////////////////////////////////////////////////////
	// 생성자, 파괴자.
	//
	// Parameters:	(int) 초기 블럭 개수, (bool)매 Alloc 호출 시 오브젝트 생성자 호출 여부
	//				(bool) Alloc 시 생성자 / Free 시 파괴자 호출 여부
	// Return:
	//////////////////////////////////////////////////////////////////////////
	CLockFreeMemoryPool(bool bPlacementNew = false);
	~CLockFreeMemoryPool();


	//////////////////////////////////////////////////////////////////////////
	// 블럭 하나를 할당받는다.  
	//
	// Parameters: 없음.
	// Return: (DATA *) 데이타 블럭 포인터.
	//////////////////////////////////////////////////////////////////////////
	DATA	*Alloc(void);

	//////////////////////////////////////////////////////////////////////////
	// 사용중이던 블럭을 해제한다.
	//
	// Parameters: (DATA *) 블럭 포인터.
	// Return: (BOOL) TRUE, FALSE.
	//////////////////////////////////////////////////////////////////////////
	bool	Free(DATA *pData);


	//////////////////////////////////////////////////////////////////////////
	// 현재 확보 된 블럭 개수를 얻는다. (메모리풀 내부의 전체 개수)
	//
	// Parameters: 없음.
	// Return: (LONG64) 메모리 풀 내부 전체 개수
	//////////////////////////////////////////////////////////////////////////
#ifndef dfLOCKFREE_MEMORYPOOL_BENCHMARKING
	LONG64		GetCapacityCount(void) { return _ldUseSize; }
	LONG64		GetPoolSize(void) { return _TotalMemoryPool; }

	inline static LONG64		GetTotalCapacityCount(void) { return _TotalUseSize; }
	inline static LONG64		GetTotalPoolSize(void) { return _TotalPoolSize; }
#endif

	//////////////////////////////////////////////////////////////////////////
	// 최대 할당 블럭 개수를 증가시킨다.
	//
	// Parameters: 없음
	// Return: 없음
	//////////////////////////////////////////////////////////////////////////
	DATA*		Resize(void);


private:
	// 스택 방식으로 반환된 (미사용) 오브젝트 블럭을 관리.
	bool alignas(64)	_bPlacementNew;	// Alloc, Free 시 매번 생성자 및 소멸자 호출 여부
	int					_iAddrDistance;	// Node와 Node 블러의 주소 차이 값(거리)

	// TLS 관련 요소
	inline static LONG64	_bIsTlsNumAlloc;
	inline static DWORD		_dwPoolTlsNum;
	inline static DWORD		_dwAllocAddrTlsNum;
	inline static DWORD		_dwAllocSizeTlsNum;
	inline static DWORD		_dwResizeAddrTlsNum;
	inline static DWORD		_dwSpareResizeAddrTlsNum;
	int						_PageCommitCount;
	LONG64					_MemCountInPage;

	st_BLOCK_NODE* alignas(64) _pFreeNode;		// 스택 Top 오브젝트 블럭

#ifndef dfLOCKFREE_MEMORYPOOL_BENCHMARKING
	LONG64 alignas(64)	_ldUseSize;			// 현재 사용중인 블럭 개수
	LONG64 alignas(64)	_TotalMemoryPool;	// 전체 블럭 개수


	inline static LONG64 alignas(64)	_TotalUseSize;	// 전체 블럭 개수
	inline static LONG64 alignas(64)	_TotalPoolSize;	// 전체 블럭 개수
#endif
};


template<class DATA>
CLockFreeMemoryPool<DATA>::CLockFreeMemoryPool(bool bPlacementNew)
	:_bPlacementNew(bPlacementNew), _pFreeNode(NULL), _iAddrDistance(0)
{
	st_BLOCK_NODE block;
	_iAddrDistance = (int)(((LONG64)(&block.data) - (LONG64)&block) / 4);

	if (0 == InterlockedExchange64(&_bIsTlsNumAlloc, true))
	{
		// TLS 번호 초기화
		_dwPoolTlsNum = TlsAlloc();
		_dwAllocAddrTlsNum = TlsAlloc();
		_dwAllocSizeTlsNum = TlsAlloc();
		_dwResizeAddrTlsNum = TlsAlloc();
		_dwSpareResizeAddrTlsNum = TlsAlloc();
	}
	
	// 페이지 내 노드 개수
	_MemCountInPage = eSYSTEM_PAGE_SIZE / sizeof(st_BLOCK_NODE);
	if (0 == _MemCountInPage)
	{
		// 노드가 단일 페이지의 크기보다 큰 경우이다.
		// 역으로 노드를 페이지 크기로 나누어 할당할 페이지의 개수를 구함
		_MemCountInPage = 1 + sizeof(st_BLOCK_NODE) / eSYSTEM_PAGE_SIZE;
		_PageCommitCount = (int)_MemCountInPage;
	}
	else
	{
		// 한 번에 커밋할 페이지 개수
		_PageCommitCount = 1;
	}

#ifndef dfLOCKFREE_MEMORYPOOL_BENCHMARKING
	_ldUseSize = 0;			
	_TotalMemoryPool = 0;	
#endif
}

template<class DATA>
CLockFreeMemoryPool<DATA>::~CLockFreeMemoryPool()
{
	st_BLOCK_NODE* pDeleteNode;
	st_BLOCK_NODE* pFreeNode = (st_BLOCK_NODE*)((ULONG64)_pFreeNode & dfLOCKFREE_MEMORYPOOL_MASKING_ADDR);

	while (NULL != pFreeNode)
	{
		pDeleteNode = pFreeNode;
		pFreeNode = pFreeNode->next;
		free(pDeleteNode);
	}
}

template<class DATA>
DATA* CLockFreeMemoryPool<DATA>::Alloc(void)
{
	DATA* retAddr;

	while (1)
	{
		st_BLOCK_NODE* pTempNode = _pFreeNode;
		st_BLOCK_NODE* pFreeNode = (st_BLOCK_NODE*)((ULONG64)pTempNode & dfLOCKFREE_MEMORYPOOL_MASKING_ADDR);
		
		if (nullptr == pFreeNode)
		{
			retAddr = Resize();
			break;
		}

		// ABA 문제를 해결하기 위하여 Top 주소 값 빈 칸에 Count 값을 계산 
		st_BLOCK_NODE* next = (st_BLOCK_NODE*)((((ULONG64)pTempNode + dfLOCKFREE_MEMORYPOOL_MASKING_COUNT_ADD) & dfLOCKFREE_MEMORYPOOL_MASKING_COUNT) | (ULONG64)(pFreeNode->next));

		if (pTempNode == (st_BLOCK_NODE*)InterlockedCompareExchange((unsigned long long*) & _pFreeNode, (unsigned long long)next, (unsigned long long)pTempNode))
		{
			retAddr = &pFreeNode->data;

#ifndef dfLOCKFREE_MEMORYPOOL_BENCHMARKING
			if (_bPlacementNew == true)
				new (retAddr) DATA;
#endif
			break;
		}
	}
#ifndef dfLOCKFREE_MEMORYPOOL_BENCHMARKING
	InterlockedIncrement64(&_ldUseSize);
	InterlockedIncrement64(&_TotalUseSize);
#endif

	return retAddr;
}

template<class DATA>
bool CLockFreeMemoryPool<DATA>::Free(DATA* pData)
{
	st_BLOCK_NODE* pNode = (st_BLOCK_NODE*)((int*)pData - _iAddrDistance);

#ifndef dfLOCKFREE_MEMORYPOOL_BENCHMARKING
	// 오브젝트 블럭 검사
	if (pNode->objectCode != (LONG64)pNode)
		return false;

	if (pNode->overflow != (LONG64)pNode)
		return false;

	// 블럭 스택 정리
	if (_bPlacementNew == true)
		pData->~DATA();
#endif

	while (1)
	{
		st_BLOCK_NODE* pTempNode = _pFreeNode;
		st_BLOCK_NODE* pFreeNode = (st_BLOCK_NODE*)((ULONG64)pTempNode & dfLOCKFREE_MEMORYPOOL_MASKING_ADDR);

		pNode->next = pFreeNode;

		// ABA 문제를 해결하기 위하여 Top 주소 값 빈 칸에 Count 값을 계산
		st_BLOCK_NODE* pNewTop = (st_BLOCK_NODE*)((((ULONG64)pTempNode + dfLOCKFREE_MEMORYPOOL_MASKING_COUNT_ADD) & dfLOCKFREE_MEMORYPOOL_MASKING_COUNT) | (ULONG64)(pNode));

		if (pTempNode == (st_BLOCK_NODE*)InterlockedCompareExchange((unsigned long long*)&_pFreeNode, (unsigned long long)pNewTop, (unsigned long long)pTempNode))
		{
			break;
		}
	}
#ifndef dfLOCKFREE_MEMORYPOOL_BENCHMARKING
	InterlockedDecrement64(&_ldUseSize);
	InterlockedDecrement64(&_TotalUseSize);
#endif

	return true;
}

template<class DATA>
DATA* CLockFreeMemoryPool<DATA>::Resize(void)
{
	// 오브젝트 블럭 할당
	LONG64 dlSpareResizeAddrCount = (LONG64)TlsGetValue(_dwSpareResizeAddrTlsNum);
	
	if (dlSpareResizeAddrCount > 0)
	{
		TlsSetValue(_dwSpareResizeAddrTlsNum, (void*)(dlSpareResizeAddrCount - 1));

		st_BLOCK_NODE* pNewBlock = (st_BLOCK_NODE*)TlsGetValue(_dwResizeAddrTlsNum);

		// 메모리풀 추가 할당 주소 업데이트
		LONG64 NextResizeBlock = (LONG64)(pNewBlock->next);
		TlsSetValue(_dwResizeAddrTlsNum, (void*)NextResizeBlock);

		return &pNewBlock->data;
	}

	LONG64 ldAllocSize = (LONG64)TlsGetValue(_dwAllocSizeTlsNum);
	int* pBaseAddr = (int*)TlsGetValue(_dwAllocAddrTlsNum);
	int* pBlockAddrBase = pBaseAddr;

	// 동적 메모리 리저브 및 커밋
	if ((NULL == pBaseAddr)
		|| ((LONG64)eALLOC_PAGE_RESERVE_SIZE - ldAllocSize < (LONG64)(sizeof(st_BLOCK_NODE) * _MemCountInPage)))
	{
		// Condition 1: 최초 메모리 예약
		// Condition 2: 예약한 64KB 공간이 할당크기만큼의 공간이 없는 경우

		pBlockAddrBase = (int*)VirtualAlloc(NULL, eALLOC_PAGE_RESERVE_SIZE, MEM_RESERVE, PAGE_NOACCESS);
		pBaseAddr = pBlockAddrBase;
		ldAllocSize = 0;
	}

	if ((NULL == pBlockAddrBase) || (ERROR_INVALID_ADDRESS == (LONG64)pBlockAddrBase))
	{
		// Condition 1: 메모리 할당 실패
		// Condition 2: 유효하지 않은 공간을 예약 요청한 경우
		int* exit = NULL;
		*exit = 0;
		return NULL;
	}

	pBlockAddrBase = (int*)VirtualAlloc(pBaseAddr, sizeof(st_BLOCK_NODE) * _MemCountInPage, MEM_COMMIT, PAGE_READWRITE);

	// 할당된 페이지 사이즈 구하기
	pBaseAddr = pBaseAddr + (eSYSTEM_PAGE_SIZE / sizeof(int)) * _PageCommitCount;
	ldAllocSize += _PageCommitCount * eSYSTEM_PAGE_SIZE;

	// TLS 갱신
	TlsSetValue(_dwAllocAddrTlsNum, pBaseAddr);
	TlsSetValue(_dwAllocSizeTlsNum, (void*)ldAllocSize);
	TlsSetValue(_dwSpareResizeAddrTlsNum, (void*)(_MemCountInPage - 1));

	// 오브젝트 블럭 할당
	st_BLOCK_NODE* pAllocBlock = (st_BLOCK_NODE*)pBlockAddrBase;
	st_BLOCK_NODE* pBlock = pAllocBlock + 1;

#ifndef dfLOCKFREE_MEMORYPOOL_BENCHMARKING
	//--------------------------------------
	// 메모리 초기화
	//--------------------------------------
	pAllocBlock->objectCode = (LONG64)pAllocBlock;
	pAllocBlock->next = nullptr;
	pAllocBlock->overflow = (LONG64)pAllocBlock;

	new (&pAllocBlock->data) DATA;

	if (_bPlacementNew == true)
	{
		pBlock->objectCode = (LONG64)pBlock;
		pBlock->next = nullptr;
		pBlock->overflow = (LONG64)pBlock;

		for (int cnt = 2; cnt < _MemCountInPage; cnt++)
		{
			st_BLOCK_NODE* pNewBlock;

			pNewBlock = (st_BLOCK_NODE*)pBlock + 1;
			pNewBlock->objectCode = (LONG64)pNewBlock;
			pNewBlock->next = pBlock;
			pNewBlock->overflow = (LONG64)pNewBlock;

			pBlock = pNewBlock;
		}
	}
	else // if (bPlacementNew == false)
	{
		pBlock->objectCode = (LONG64)pBlock;
		pBlock->next = nullptr;
		pBlock->overflow = (LONG64)pBlock;

		// placement new로 생성자 호출
		new (&pBlock->data) DATA;

		for (int cnt = 2; cnt < _MemCountInPage; cnt++)
		{
			st_BLOCK_NODE* pNewBlock;

			pNewBlock = (st_BLOCK_NODE*)pBlock + 1;
			pNewBlock->objectCode = (LONG64)pNewBlock;
			pNewBlock->next = pBlock;
			pNewBlock->overflow = (LONG64)pNewBlock;

			pBlock = pNewBlock;

			new (&pBlock->data) DATA;
		}
	}
#else
	//--------------------------------------
	// 메모리 초기화
	//--------------------------------------

	// 할당 메모리
	pAllocBlock->objectCode = (LONG64)pAllocBlock;
	pAllocBlock->next = nullptr;
	pAllocBlock->overflow = (LONG64)pAllocBlock;

	new (&pAllocBlock->data) DATA;

	// 메모리풀 스택
	pBlock->objectCode = (LONG64)pBlock;
	pBlock->next = nullptr;
	pBlock->overflow = (LONG64)pBlock;

	new (&pBlock->data) DATA;

	for (int cnt = 2; cnt < _MemCountInPage; cnt++)
	{
		st_BLOCK_NODE* pNewBlock;

		pNewBlock = (st_BLOCK_NODE*)pBlock + 1;
		pNewBlock->objectCode = (LONG64)pNewBlock;
		pNewBlock->next = pBlock;
		//pNewBlock->next = nullptr;
		pNewBlock->overflow = (LONG64)pNewBlock;

		pBlock = pNewBlock;

		new (&pBlock->data) DATA;
	}
#endif
	
	// 메모리풀 추가 할당 주소 업데이트
	TlsSetValue(_dwResizeAddrTlsNum, pBlock);

#ifndef dfLOCKFREE_MEMORYPOOL_BENCHMARKING
	InterlockedIncrement64(&_TotalMemoryPool);
	InterlockedAdd64(&_TotalPoolSize, _MemCountInPage);
#endif

	return &pAllocBlock->data;
}


#endif
