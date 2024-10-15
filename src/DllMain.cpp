#include <libsm64.h>
#include <surface_terrains.h>

using namespace hh::fnd;
using namespace hh::game;
using namespace hh::physics;
using namespace hh::gfx;

Eigen::Vector3f MatrixToEuler(const Eigen::Matrix3f& mat) {
	auto absoluteEuler = mat.eulerAngles(1, 0, 2);

	return { absoluteEuler[1], absoluteEuler[0], absoluteEuler[2] };
}

class ResN64Rom : public ManagedResource {
public:
	uint8_t* rom;

	virtual void Load(void* data, size_t size) override {
		rom = (uint8_t*)data;
	}

	virtual void Unload() override { }

	MANAGED_RESOURCE_CLASS_DECLARATION(ResN64Rom);
};

ResN64Rom::ResN64Rom(csl::fnd::IAllocator* allocator) : ManagedResource{ allocator } {}
ManagedResource* ResN64Rom::Create(csl::fnd::IAllocator* allocator) { return new (allocator) ResN64Rom(allocator); }
const ResourceTypeInfo* ResN64Rom::GetTypeInfo() { return &typeInfo; }
const ResourceTypeInfo ResN64Rom::typeInfo{ "ResN64Rom", "ResN64Rom", sizeof(ResN64Rom), false, &ResN64Rom::Create };

SM64ObjectTransform ToObjectTransform(const csl::math::Vector3& position, const csl::math::Quaternion& rotation) {
	Eigen::Vector3f rot = MatrixToEuler(rotation.toRotationMatrix());

	SM64ObjectTransform otf{};
	memcpy(otf.position, position.data(), sizeof(float) * 3);
	memcpy(otf.eulerRotation, rot.data(), sizeof(float) * 3);
	return otf;
}
SM64ObjectTransform ToObjectTransform(const WorldPosition& worldPos) {
	return ToObjectTransform(worldPos.m_Position, worldPos.m_Rotation);
}

uint32_t CreateBoxSurfaceObject(csl::fnd::IAllocator* allocator, GOCBoxCollider* coll) {
	SM64ObjectTransform trans = ToObjectTransform(coll->transformedWorldPosition);
	SM64Surface surfaces[12];

	Geometry geom{ allocator };
	geom.CreateBox({ 0.0f, 0.0f, 0.0f }, coll->dimensions * 10.0f, { 0.0f, 0.0f, 0.0f, 1.0f });
	
	for (int i = 0; i < 12; i++) {
		auto v1 = geom.vertices[geom.triangles[i].v1];
		auto v2 = geom.vertices[geom.triangles[i].v2];
		auto v3 = geom.vertices[geom.triangles[i].v3];

		surfaces[i] = { SURFACE_DEFAULT, 0, TERRAIN_GRASS, { { static_cast<int>(v1.x()), static_cast<int>(v1.y()), static_cast<int>(v1.z()) }, { static_cast<int>(v2.x()), static_cast<int>(v2.y()), static_cast<int>(v2.z()) }, { static_cast<int>(v3.x()), static_cast<int>(v3.y()), static_cast<int>(v3.z()) } } };
	}

	SM64SurfaceObject obj{ trans, 12, surfaces };

	return sm64_surface_object_create(&obj);
}

class LibSM64Service : public GameService, GameManagerListener, PhysicsWorldListener, GameStepListener {
	uint8_t texture[SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT * 4];
	csl::ut::PointerMap<GOCBoxCollider*, uint32_t> boxObjs{ GetAllocator() };

public:
	hh::needle::intrusive_ptr<hh::needle::Texture> needleTexture{};
	virtual void OnAddedToGame() override {
		ResN64Rom* romResource = ResourceManager::GetInstance()->GetResource<ResN64Rom>("sm64.z64");
		sm64_global_init(romResource->rom, texture);

		SM64Surface surf;
		surf.terrain = SURFACE_DEFAULT;
		surf.force = 0;
		surf.type = TERRAIN_GRASS;
		surf.vertices[0][0] = -2000;
		surf.vertices[0][1] = 130;
		surf.vertices[0][2] = -2000;
		surf.vertices[1][0] = 0;
		surf.vertices[1][1] = 130;
		surf.vertices[1][2] = 2000;
		surf.vertices[2][0] = 2000;
		surf.vertices[2][1] = 130;
		surf.vertices[2][2] = -2000;
		sm64_static_surfaces_load(&surf, 1);

		auto* renderDev = static_cast<hh::gfx::RenderManager*>(hh::gfx::RenderManager::GetInstance())->GetNeedleResourceDevice();

		hh::needle::TextureCreationInfo::SubresourceData subresourceDatas[1];
		subresourceDatas[0] = { texture, SM64_TEXTURE_WIDTH * 4 };

		hh::needle::TextureCreationInfo tci{};
		tci.type = hh::needle::SurfaceType::UNK3;
		tci.format = hh::needle::SurfaceFormat::R8G8B8A8;
		tci.width = SM64_TEXTURE_WIDTH;
		tci.height = SM64_TEXTURE_HEIGHT;
		tci.depth = 1;
		tci.mipLevels = 1;
		tci.arraySize = 1;
		tci.bindFlags.m_dummy = 1;
		tci.miscFlags.m_dummy = 0;
		tci.usage = hh::needle::TextureCreationInfo::Usage::DEFAULT;
		tci.subresourceDatas = subresourceDatas;
		needleTexture = renderDev->GetRenderingDevice()->CreateTexture(tci);

		//GameManager::GetInstance()->AddListener(this);
		//GameManager::GetInstance()->RegisterGameStepListener(*this);
	}

	virtual void OnRemovedFromGame() override {
		//GameManager::GetInstance()->UnregisterGameStepListener(*this);
		//GameManager::GetInstance()->RemoveListener(this);

		sm64_global_terminate();
	}

	virtual void GameServiceAddedCallback(GameService* gameService) {
		if (gameService->pStaticClass == hh::physics::PhysicsWorldBullet::GetClass())
			static_cast<hh::physics::PhysicsWorldBullet*>(gameService)->AddListener(this);
	}

	virtual void GameServiceRemovedCallback(GameService* gameService) {
		if (gameService->pStaticClass == hh::physics::PhysicsWorldBullet::GetClass())
			static_cast<hh::physics::PhysicsWorldBullet*>(gameService)->RemoveListener(this);
	}

	virtual void ColliderAddedCallback(GOCCollider* component) override {
		if (component->pStaticClass != GOCBoxCollider::GetClass())
			return;

		auto* coll = static_cast<GOCBoxCollider*>(component);

		boxObjs.Insert(coll, CreateBoxSurfaceObject(GetAllocator(), coll));
	}

	virtual void ColliderRemovedCallback(GOCCollider* component) override {
		if (component->pStaticClass != GOCBoxCollider::GetClass())
			return;

		auto* coll = static_cast<GOCBoxCollider*>(component);

		auto i = boxObjs.Find(coll);

		if (i == boxObjs.end())
			return;

		sm64_surface_object_delete(*i);
		boxObjs.Erase(i);
	}

	virtual void UpdateCallback(GameManager* gameManager, const GameStepInfo& gameStepInfo) override {
		//for (auto i = boxObjs.begin(); i != boxObjs.end(); i++) {
		//	auto* coll = i.key();

		//	SM64ObjectTransform tf = ToObjectTransform(coll->transformedWorldPosition);
		//	sm64_surface_object_move(*i, &tf);
		//}
	}

	GAMESERVICE_CLASS_DECLARATION(LibSM64Service);
};

LibSM64Service::LibSM64Service(csl::fnd::IAllocator* allocator) : GameService{ allocator } {}
GameService* LibSM64Service::Create(csl::fnd::IAllocator* allocator) { return new (allocator) LibSM64Service(allocator); }
const GameServiceClass LibSM64Service::gameServiceClass{ "LibSM64Service", &LibSM64Service::Create, nullptr };
const GameServiceClass* LibSM64Service::GetClass() { return &gameServiceClass; }

const size_t SM64_GEO_MAX_VERTICES = 3 * SM64_GEO_MAX_TRIANGLES;

Eigen::Affine3f TransformToAffine3f(const csl::math::Transform& transform) {
	Eigen::Affine3f affine;
	affine.fromPositionOrientationScale(transform.position, transform.rotation, transform.scale);
	return affine;
}

class Mario : public GameObject {
	float positions[SM64_GEO_MAX_VERTICES * 3]{};
	float normals[SM64_GEO_MAX_VERTICES * 3]{};
	float colors[SM64_GEO_MAX_VERTICES * 3]{};
	float uvs[SM64_GEO_MAX_VERTICES * 2]{};
	SM64MarioGeometryBuffers geoBufs{ positions, normals, colors, uvs, 0 };
	GOCVisualUserModel::Vertex vertices[SM64_GEO_MAX_VERTICES];
	unsigned short indices[SM64_GEO_MAX_VERTICES];
	unsigned int marioId;
	float prevTime;
	unsigned short prevTriangleCount{};
	/*SM64MarioState state;*/

public:
	virtual void AddCallback(GameManager* gameManager) override {
		auto* gocUserModel = CreateComponent<GOCVisualUserModel>();
		auto* resMgr = ResourceManager::GetInstance();

		GOCVisualUserModel::Description::UserModelDescription model{};
		model.vertexCount = SM64_GEO_MAX_VERTICES;
		model.vertices = vertices;
		model.indexCount = SM64_GEO_MAX_VERTICES;
		model.indices = indices;
		model.pixelShader = resMgr->GetResource<hh::gfnd::ResFragmentShader>("Common_d")->shader;
		//model.pixelShader = resMgr->GetResource<hh::gfnd::ResFragmentShader>("IgnoreLight_d")->shader;
		model.qword24 = 3;
		model.qword28 = 0;

		GOCVisualUserModel::Description desc{};
		desc.userModel = model;
		desc.dword1C = 0; //3;
		desc.byteD0 = 1;
		desc.flags.set(GOCVisualModelDescription::Flag::IS_SHADOW_CASTER, true);

		hh::needle::CNameIDObject* parameterNames[1]{ RESOLVE_STATIC_VARIABLE(hh::needle::StaticIDsTemp::commonDDiffuse).uniqueObject };
		desc.shaderParameter.SetParameter({ parameterNames, 1 });

		float value[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		gocUserModel->Setup(desc);
		gocUserModel->SetTexture(RESOLVE_STATIC_VARIABLE(hh::needle::StaticIDsTemp::commonDDiffuse).uniqueObject, resMgr->GetResource<hh::gfnd::ResTexture>("cmn_obj_emerald_out_white_ems"));
		//gocUserModel->SetTexture(RESOLVE_STATIC_VARIABLE(hh::needle::StaticIDsTemp::commonDDiffuse).uniqueObject, gameManager->GetService<LibSM64Service>()->needleTexture);
		gocUserModel->SetShaderParameterFloatByName(RESOLVE_STATIC_VARIABLE(hh::needle::StaticIDsTemp::commonDDiffuseColor).uniqueObject, value, 1);
		gocUserModel->SetShaderParameterFloatByName(RESOLVE_STATIC_VARIABLE(hh::needle::StaticIDsTemp::commonDAmbientColor).uniqueObject, value, 1);
		gocUserModel->SetShaderParameterFloatByName(RESOLVE_STATIC_VARIABLE(hh::needle::StaticIDsTemp::commonDEmissiveColor).uniqueObject, value, 1);
		gocUserModel->SetShaderParameterFloatByName(RESOLVE_STATIC_VARIABLE(hh::needle::StaticIDsTemp::commonDEmissiveColor).uniqueObject, value, 1);
		AddComponent(gocUserModel);

		gocUserModel->SetVisible(true);

		auto* gocInput = CreateComponent<GOCInput>();
		gocInput->Initialize({
			.unk1 = true,
			.inputComponentInternalPlayerInputIndex = 0,
			.inputComponentPriority = 20,
			.inputComponentActionMonitorCount = 1,
			.inputComponentAxisMonitorCount = 8,
			.inputComponentUnk7 = 1,
			});
		AddComponent(gocInput);

		auto* inputComponent = gocInput->GetInputComponent();
		inputComponent->MonitorAxisMapping("PlayerMoveLeft", 0);
		inputComponent->MonitorAxisMapping("PlayerMoveRight", 1);
		inputComponent->MonitorAxisMapping("PlayerMoveUp", 2);
		inputComponent->MonitorAxisMapping("PlayerMoveDown", 3);
		inputComponent->MonitorAxisMapping("CameraMoveLeft", 4);
		inputComponent->MonitorAxisMapping("CameraMoveRight", 5);
		inputComponent->MonitorAxisMapping("CameraMoveUp", 6);
		inputComponent->MonitorAxisMapping("CameraMoveDown", 7);
		inputComponent->MonitorActionMapping("PlayerJump", 0, 2);

		auto* gocTransform = GetComponent<GOCTransform>();
		auto pos = gocTransform->frame->fullTransform.position;

		//state.position[0] = pos.x();
		//state.position[1] = pos.y();
		//state.position[2] = pos.z();
		marioId = sm64_mario_create(pos.x(), pos.y(), pos.z());

		prevTime = gameManager->globalTime;
	}

	virtual void RemoveCallback(GameManager* gameManager) override {
		sm64_mario_delete(marioId);
	}

	virtual void Update(UpdatingPhase phase, const SUpdateInfo& updateInfo) override {
		auto* gocInput = GetComponent<GOCInput>();
		auto* inputComponent = gocInput->GetInputComponent();


		if ((gameManager->globalTime - prevTime) < (1.0f / 30.0f))
			return;

		SM64MarioState state{};

		while ((gameManager->globalTime - prevTime) >= (1.0f / 30.0f)) {
			SM64MarioInputs inputs{};
			inputs.camLookX = inputComponent->axisMonitors[4].state + inputComponent->axisMonitors[5].state;
			inputs.camLookZ = inputComponent->axisMonitors[7].state + inputComponent->axisMonitors[6].state;
			inputs.stickX = inputComponent->axisMonitors[0].state + inputComponent->axisMonitors[1].state;
			inputs.stickY = inputComponent->axisMonitors[3].state + inputComponent->axisMonitors[2].state;
			inputs.buttonA = inputComponent->actionMonitors[0].state > 0;
			inputs.buttonB = 0;
			inputs.buttonZ = 0;

			sm64_mario_tick(marioId, &inputs, &state, &geoBufs);

			prevTime += 1.0f / 30.0f;
		}

		auto* gocTransform = GetComponent<GOCTransform>();

		//gocTransform->SetLocalTranslation({ state.position[0], state.position[1], state.position[2] });

		Eigen::Affine3f inverseTf = TransformToAffine3f(gocTransform->frame->fullTransform).inverse();

		for (unsigned short i = 0; i < geoBufs.numTrianglesUsed; i++)
			LoadTriangle(geoBufs, inverseTf, i);

		for (unsigned short i = geoBufs.numTrianglesUsed; i < prevTriangleCount; i++)
			ClearTriangle(geoBufs, i);

		prevTriangleCount = geoBufs.numTrianglesUsed;

		auto* model = GetComponent<GOCVisualUserModel>();
		model->SetVertexBuffer(vertices);
	}

	inline void LoadTriangle(const SM64MarioGeometryBuffers& geoBufs, const Eigen::Affine3f& inverseTf, unsigned short i) {
		vertices[i * 3 + 0] = GetVertex(geoBufs, inverseTf, i * 3 + 0);
		vertices[i * 3 + 1] = GetVertex(geoBufs, inverseTf, i * 3 + 1);
		vertices[i * 3 + 2] = GetVertex(geoBufs, inverseTf, i * 3 + 2);
	}

	inline void ClearTriangle(const SM64MarioGeometryBuffers& geoBufs, unsigned short i) {
		vertices[i * 3 + 0] = { csl::math::Vector3{ 0.0f, 0.0f, 0.0f }, csl::ut::Color8{ 0, 0, 0, 0 }, csl::math::Vector2{ 0.0f, 0.0f } };
		vertices[i * 3 + 1] = { csl::math::Vector3{ 0.0f, 0.0f, 0.0f }, csl::ut::Color8{ 0, 0, 0, 0 }, csl::math::Vector2{ 0.0f, 0.0f } };
		vertices[i * 3 + 2] = { csl::math::Vector3{ 0.0f, 0.0f, 0.0f }, csl::ut::Color8{ 0, 0, 0, 0 }, csl::math::Vector2{ 0.0f, 0.0f } };
	}

	inline static GOCVisualUserModel::Vertex GetVertex(const SM64MarioGeometryBuffers& geoBufs, const Eigen::Affine3f& inverseTf, unsigned short i) {
		GOCVisualUserModel::Vertex res = {
			(inverseTf * csl::math::Vector3{ geoBufs.position[i * 3 + 0], geoBufs.position[i * 3 + 1], geoBufs.position[i * 3 + 2] }) * 0.01f,
			csl::ut::Color8{ static_cast<uint8_t>(geoBufs.color[i * 3 + 0] * 255), static_cast<uint8_t>(geoBufs.color[i * 3 + 1] * 255), static_cast<uint8_t>(geoBufs.color[i * 3 + 2] * 255), 255 },
			csl::math::Vector2{ geoBufs.uv[i * 2 + 0], geoBufs.uv[i * 2 + 1] }
		};

		res.normal = { geoBufs.normal[i * 3 + 0], geoBufs.normal[i * 3 + 1], geoBufs.normal[i * 3 + 2] };

		return res;
	}

	GAMEOBJECT_CLASS_DECLARATION(Mario);
};

const GameObjectClass Mario::gameObjectClass{ "Mario", "Mario", sizeof(Mario), &Mario::Create, 0, nullptr, nullptr };
const GameObjectClass* Mario::GetClass() { return &gameObjectClass; }
GameObject* Mario::Create(csl::fnd::IAllocator* allocator) { return new (allocator) Mario(allocator); }
Mario::Mario(csl::fnd::IAllocator* allocator) : GameObject{ allocator } {
	for (unsigned short i = 0; i < SM64_GEO_MAX_VERTICES; i++)
		vertices[i] = { csl::math::Vector3{ 0.0f, 0.0f, 0.0f }, csl::ut::Color8{ 0, 0, 0, 0 }, csl::math::Vector2{ 0.0f, 0.0f } };

	for (unsigned short i = 0; i < SM64_GEO_MAX_VERTICES; i++)
		indices[i] = i;

	SetUpdateFlag(UpdatingPhase::PRE_ANIM, true);
}

class KeyEventHandler : public hh::fw::KeyEventHandler, public hh::game::GameManagerListener {
	virtual bool OnKeyDown(hh::fw::KeyEventArgs& keyEventArgs) override {
		if (keyEventArgs.scanCode >= 0x1e && keyEventArgs.scanCode <= 0x21) {
			if (auto* gameManager = hh::game::GameManager::GetInstance())
			if (auto* levelInfo = gameManager->GetService<app::level::LevelInfo>())
			if (auto* fxParamMgr = gameManager->GetService<app::gfx::FxParamManager>()) {
				app::player::Player::Kill(gameManager, 0);

				gameManager->ShutdownPendingObjects();

				auto& stageConfig = fxParamMgr->sceneParameters[fxParamMgr->currentSceneParameters]->sceneData->stageConfig;
				auto* playerInfo = levelInfo->GetPlayerInformation(0);

				WorldPosition pos{};
				pos.m_Position = playerInfo->position.value;
				pos.m_Rotation = playerInfo->rotation.value;

				auto* mario = GameObject::Create<Mario>(gameManager->GetAllocator());
				gameManager->AddGameObject(mario, nullptr, false, &pos, nullptr);
				//playerInfo->playerObject.value = mario;
				//playerInfo->playerObject.isSet = true;
			}
		}
		return false;
	}
};

KeyEventHandler keyEventHandler;


ResourceLoader* resourceLoader;


void printDebug(const char* str) {
	printf("%s\n", str);
}

HOOK(uint64_t, __fastcall, GameApplication_Reset, 0x1501A41F0, hh::game::GameApplication* self) {
	auto res = originalGameApplication_Reset(self);

	ResourceLoader::Locale locale{};
	resourceLoader = hh::fnd::ResourceLoader::Create(hh::fnd::MemoryRouter::GetModuleAllocator());
	resourceLoader->LoadResource(InplaceTempUri{ "roms/sm64.z64" }, ResN64Rom::GetTypeInfo(), 0, 1, locale);

	return res;
}

HOOK(uint64_t, __fastcall, GameModeTitleInit, 0x1401FD8B0, app::game::GameMode* self) {
	auto res = originalGameModeTitleInit(self);

	sm64_register_debug_print_function(printDebug);

	auto* sm64Srv = GameManager::GetInstance()->CreateService<LibSM64Service>(hh::fnd::MemoryRouter::GetModuleAllocator());
	GameManager::GetInstance()->RegisterService(sm64Srv);
	hh::game::GameApplication::GetInstance()->AddKeyEventHandler(&keyEventHandler, 0);

	return res;
}

HOOK(hh::fnd::ResourceTypeRegistry*, __fastcall, ResourceTypeRegistry_Create, 0x152EFD7F0) {
	auto* res = originalResourceTypeRegistry_Create();
	res->RegisterTypeInfo(ResN64Rom::GetTypeInfo());
	res->RegisterExtension("z64", ResN64Rom::GetTypeInfo());
	return res;
}

BOOL WINAPI DllMain(_In_ HINSTANCE hInstance, _In_ DWORD reason, _In_ LPVOID reserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		INSTALL_HOOK(GameApplication_Reset);
		INSTALL_HOOK(GameModeTitleInit);
		INSTALL_HOOK(ResourceTypeRegistry_Create);
		break;
	case DLL_PROCESS_DETACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}

	return TRUE;
}
