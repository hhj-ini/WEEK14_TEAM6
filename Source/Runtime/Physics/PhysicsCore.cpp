#include "EnginePCH.h"
#include "PhysicsCore.h"
#include "PhysXIncludes.h"
#include "PhysicalMaterial.h"
#include "UObject/UObjectGlobals.h"

// PhysX가 Foundation 생성에 요구하는 어댑터만 둔다. 상태(대기열 등)는 FPhysicsCore가 갖는다.
// 둘 다 Foundation보다 오래 살아야 하므로 파일 범위 객체로 둔다.
namespace
{
	physx::PxDefaultAllocator Allocator;

	class FPhysXErrorCallback : public physx::PxErrorCallback
	{
	public:
		void reportError(physx::PxErrorCode::Enum Code, const char* Message, const char* File, int Line) override
		{
			ELogVerbosity Verbosity = ELogVerbosity::Error;
			switch (Code)
			{
			case physx::PxErrorCode::eDEBUG_INFO:
				Verbosity = ELogVerbosity::Info;
				break;
			case physx::PxErrorCode::eDEBUG_WARNING:
			case physx::PxErrorCode::ePERF_WARNING:
				Verbosity = ELogVerbosity::Warning;
				break;
			default:
				break;
			}
			FPhysicsCore::Get().EnqueueLog(Verbosity, std::format("[PhysX] {} ({}:{})", Message, File, Line));
		}
	};

	FPhysXErrorCallback ErrorCallback;
}

bool FPhysicsCore::Init()
{
	MainThreadId = std::this_thread::get_id();
	if (Foundation) 
		return true;
	Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, Allocator, ErrorCallback);

	if (!Foundation)
	{
		KRS_LOG(Error, "PxCreateFoundation failed!");
		return false;
	}
	KRS_LOG(Info, "PhysX Foundation created.");

	// PVD는 Physics보다 먼저 만들어 PxCreatePhysics에 넘긴다. 연결 실패는 Init 실패가 아니다.
	InitPvd();

	// 엔진 월드 단위가 m이므로 기본 스케일을 그대로 쓴다.
	// Debug에서는 할당을 추적해 Foundation 해제 때 남은 객체를 에러 콜백으로 보고받는다.
#if defined(_DEBUG)
	constexpr bool bTrackAllocations = true;
#else
	constexpr bool bTrackAllocations = false;
#endif
	Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *Foundation, physx::PxTolerancesScale(), bTrackAllocations, Pvd);
	if (!Physics)
	{
		KRS_LOG(Error, "PxCreatePhysics failed!");
		Shutdown();
		return false;
	}

	// D6 Joint 등 조인트와 PVD 조인트 시각화가 Extensions에 있다.
	bExtensionsInitialized = PxInitExtensions(*Physics, Pvd);
	if (!bExtensionsInitialized)
	{
		KRS_LOG(Error, "PxInitExtensions failed!");
		Shutdown();
		return false;
	}

	// 자동차(PxVehicleDrive4W 등)는 이 초기화가 필요하다. 프로세스당 한 번만 부른다.
	bVehicleSDKInitialized = physx::PxInitVehicleSDK(*Physics);
	if (!bVehicleSDKInitialized)
	{
		KRS_LOG(Error, "PxInitVehicleSDK failed!");
		Shutdown();
		return false;
	}

	Cooking = PxCreateCooking(PX_PHYSICS_VERSION, *Foundation, physx::PxCookingParams(Physics->getTolerancesScale()));
	if (!Cooking)
	{
		KRS_LOG(Error, "PxCreateCooking failed!");
		Shutdown();
		return false;
	}

	// 월드(에디터/PIE/프리뷰)마다 만들지 않고 모든 씬이 공유한다.
	Dispatcher = physx::PxDefaultCpuDispatcherCreate(4);
	if (!Dispatcher)
	{
		KRS_LOG(Error, "PxDefaultCpuDispatcherCreate failed!");
		Shutdown();
		return false;
	}

	// 실패 경로의 Shutdown이 Physics를 먼저 해제하지 않도록 마지막에 만든다.
	// (UPhysicalMaterial 소멸자가 PxMaterial을 release하므로 Physics보다 먼저 지워져야 한다)
	DefaultPhysMaterial = NewObject<UPhysicalMaterial>();
	DefaultPhysMaterial->StaticFriction = 0.5f;
	DefaultPhysMaterial->Friction = 0.5f;
	DefaultPhysMaterial->Restitution = 0.1f;
	if (!DefaultPhysMaterial->GetPxMaterial())
	{
		KRS_LOG(Error, "Failed to create default PhysX material!");
		Shutdown();   // PxMaterial이 없으니 남은 UObject가 나중에 지워져도 release할 것이 없다
		return false;
	}

	KRS_LOG(Info, "PhysX initialized.");

	return true;
}

void FPhysicsCore::EnqueueLog(ELogVerbosity Verbosity, FString Message)
{
	if (std::this_thread::get_id() == MainThreadId)
	{
		FLog::Emit(Verbosity, Message);
		return;
	}

	std::lock_guard<std::mutex> Lock(PendingLogMutex);
	PendingLogs.Add(FPendingLog{ Verbosity, std::move(Message) });
}

void FPhysicsCore::FlushPendingLogs()
{
	// 락을 잡은 채 로그를 보내지 않도록 먼저 꺼내 온다.
	TArray<FPendingLog> Logs;
	{
		std::lock_guard<std::mutex> Lock(PendingLogMutex);
		Logs = std::move(PendingLogs);
		PendingLogs.Empty();
	}
	for (const FPendingLog& Log : Logs)
	{
		FLog::Emit(Log.Verbosity, Log.Message);
	}
}

void FPhysicsCore::Shutdown()
{
	if (LiveSceneCount > 0)
	{
		KRS_LOG(Warning, "FPhysicsCore::Shutdown called while {} PhysX scenes are still alive!", LiveSceneCount);
	}
	if (Dispatcher)
	{
		Dispatcher->release();
		Dispatcher = nullptr;
	}
	// 기본 머티리얼(UObject)은 DestroyAllObjectsExceptEngine이 이미 지웠다.
	DefaultPhysMaterial = nullptr;
	if (bVehicleSDKInitialized)
	{
		physx::PxCloseVehicleSDK();
		bVehicleSDKInitialized = false;
	}
	if (bExtensionsInitialized)
	{
		PxCloseExtensions();
		bExtensionsInitialized = false;
	}
	if (Cooking)
	{
		Cooking->release();
		Cooking = nullptr;
	}
	if (Physics)
	{
		Physics->release();
		Physics = nullptr;
	}
	// Pvd가 release 중에 Transport로 연결을 끊으므로 Pvd를 먼저 해제한다.
	if (Pvd)
	{
		Pvd->release();
		Pvd = nullptr;
	}
	if (PvdTransport)
	{
		PvdTransport->release();
		PvdTransport = nullptr;
	}
	if (Foundation)
	{
		Foundation->release();
		Foundation = nullptr;
	}
	// 해제 도중 생긴 메시지(누수 보고 등)까지 내보낸다.
	FlushPendingLogs();
	KRS_LOG(Info, "PhysX shutdown complete.");
}

bool FPhysicsCore::InitPvd()
{ 
	Pvd = PxCreatePvd(*Foundation);
	if (!Pvd)
	{
		KRS_LOG(Warning, "PxCreatePvd failed!");
		return false;
	}

	PvdTransport = physx::PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
	if (!PvdTransport)
	{
		KRS_LOG(Warning, "PxDefaultPvdSocketTransportCreate failed!");
		return false;
	}

	// PVD 프로그램이 꺼져 있으면 연결이 안 될 뿐이다. 에러가 아니다.
	if (!Pvd->connect(*PvdTransport, physx::PxPvdInstrumentationFlag::eALL))
	{
		KRS_LOG(Info, "PhysX PVD not running (127.0.0.1:5425). Continuing without PVD.");
		return false;
	}
	KRS_LOG(Info, "PhysX PVD connected.");
	return true;
}