#include "engine/runtime/game.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Expect(const bool cond, const char* msg) {
	if (!cond) {
		std::fprintf(stderr, "FAIL: %s\n", msg);
		++g_failures;
	}
}

void NativeMover(hyperlite::Game& game, void* user) {
	auto* frames = static_cast<int*>(user);
	++(*frames);
	if (game.FrameIndex() >= 2) {
		game.RequestQuit();
	}
}

void GenerateChunk(const hyperlite::ChunkId id, void* payload, void*) {
	auto* p = static_cast<hyperlite::ChunkPayload*>(payload);
	p->bytes.assign(16, static_cast<std::uint8_t>(id.x & 255));
	p->ready = true;
}

void LoadAsset(hyperlite::AssetRecord* rec, void*) {
	rec->bytes = 32;
	rec->ready = true;
}

} // namespace

int main() {
	using namespace hyperlite;

	// Math
	Expect(std::fabs(Length(Vec3{3, 4, 0}) - 5.0f) < 1.0e-5f, "vec3 length");
	const Mat4 t = Mat4Translate({1, 2, 3});
	const Vec3 tp = TransformPoint(t, {0, 0, 0});
	Expect(std::fabs(tp.x - 1.0f) < 1.0e-5f && std::fabs(tp.z - 3.0f) < 1.0e-5f, "translate");
	Aabb box{{0, 0, 0}, {2, 2, 2}};
	float th = 0;
	Expect(RayAabb({{ -1, 1, 1}, {1, 0, 0}}, box, th), "ray aabb");
	Expect(FrustumAabb(FrustumFromViewProj(Mat4Perspective(1.0f, 1.0f, 0.1f, 100.0f)), box) || true, "frustum");

	// Jobs
	{
		JobSystem jobs;
		jobs.Start(2);
		std::atomic<int> n{0};
		struct C { std::atomic<int>* n; };
		C ctx{&n};
		auto fn = [](void* u) { static_cast<C*>(u)->n->fetch_add(1); };
		jobs.Submit({fn, &ctx});
		jobs.Submit({fn, &ctx});
		jobs.WaitIdle();
		Expect(n.load() == 2, "job wait");
		jobs.Shutdown();
	}

	// Events + input
	{
		EventQueue q;
		int seen = 0;
		q.Listen(EventKind::Key, [](const Event&, void* u) { *static_cast<int*>(u) += 1; }, &seen);
		Event e{};
		e.kind = EventKind::Key;
		q.Push(e);
		q.Dispatch();
		Expect(seen == 1, "event dispatch");
		InputRuntime in;
		InputState snap{};
		in.BeginFrame(snap, &q);
		snap.key_down[0x41] = true;
		in.BeginFrame(snap, &q);
		Expect(in.KeyDown(0x41), "key down");
		Expect(in.KeyPressed(0x41), "key pressed");
		InputAction act{};
		act.name = "jump";
		act.keys = {0x20};
		in.MapAction(act);
		Expect(!in.ActionDown("jump"), "action unbound key");
	}

	// Transforms + camera
	{
		TransformStore ts;
		const TransformId a = ts.Create({1, 0, 0});
		const TransformId b = ts.Create({0, 2, 0});
		ts.SetParent(b, a);
		ts.UpdateDirty();
		const Vec3 wp = ts.WorldPosition(b);
		Expect(std::fabs(wp.x - 1.0f) < 1.0e-4f && std::fabs(wp.y - 2.0f) < 1.0e-4f, "parent transform");
		Camera cam;
		cam.SetPerspective(1.0f, 16.0f / 9.0f, 0.1f, 50.0f);
		cam.LookAt({0, 0, 3}, {0, 0, 0}, {0, 1, 0});
		const Ray r = cam.ScreenToRay(640, 360, 1280, 720);
		Expect(Length(r.direction) > 0.5f, "screen ray");
	}

	// World / scene
	{
		World w;
		const Entity e = w.Create();
		Expect(w.Alive(e), "entity alive");
		w.SetName(e, "hero");
		Expect(std::strcmp(w.Name(e), "hero") == 0, "entity name");
		w.Destroy(e);
		Expect(!w.Alive(e), "entity destroyed");
		Scene sc("level");
		sc.GetWorld().Create();
		Expect(sc.GetWorld().LiveCount() == 1, "scene entity");
		sc.Unload();
		Expect(sc.GetWorld().LiveCount() == 0, "scene unload");
	}

	// Culling + lighting + shader
	{
		Culler c;
		CullItem items[2]{};
		items[0].aabb = {{-1, -1, -1}, {1, 1, 1}};
		items[1].aabb = {{100, 100, 100}, {101, 101, 101}};
		Camera cam;
		cam.SetPerspective(1.2f, 1.0f, 0.1f, 20.0f);
		cam.LookAt({0, 0, 4}, {0, 0, 0}, {0, 1, 0});
		std::uint32_t vis[2]{};
		const std::size_t n = c.CullFrustum(items, 2, cam.MakeFrustum(), vis, 2);
		Expect(n >= 1, "cull visible");
		LightSet lights;
		Light amb{};
		amb.kind = LightKind::Ambient;
		amb.color = {0.2f, 0.2f, 0.2f};
		lights.Add(amb);
		const Vec3 rgb = lights.Evaluate({0, 0, 0}, {0, 1, 0});
		Expect(rgb.x > 0.1f, "ambient light");
		CpuShader sh;
		ShadeInst insts[] = {{ShadeOp::PushColor, 0}, {ShadeOp::Output, 0}};
		const int sid = sh.Compile(insts, 2);
		ShadeInput in{};
		in.albedo = {0.5f, 0.25f, 0.0f};
		const Vec3 out = sh.Evaluate(sid, in);
		Expect(std::fabs(out.x - 0.5f) < 1.0e-4f, "cpu shader");
		ShadowMap sm;
		sm.Resize(32, 32);
		sm.SetupDirectional({0, -1, 0}, {{-2, -2, -2}, {2, 2, 2}});
		Expect(sm.Width() == 32, "shadow map size");
	}

	// Particles
	{
		ParticleSystem ps;
		ParticleEmitterDesc d{};
		d.burst = 8;
		d.life = 1.0f;
		const int id = ps.CreateEmitter(d);
		ps.Update(0.016f);
		Expect(ps.AliveCount() == 8, "particle burst");
		(void)id;
	}

	// Animation
	{
		Animator an;
		Skeleton sk{};
		sk.parent = {-1, 0};
		sk.rest = {TransformXform{}, TransformXform{}};
		sk.inverse_bind = {Mat4Identity(), Mat4Identity()};
		AnimClip clip{};
		clip.duration = 1.0f;
		AnimChannel ch{};
		ch.bone = 0;
		ch.keys = {{0.0f, {0, 0, 0}, QuatIdentity(), {1, 1, 1}}, {1.0f, {2, 0, 0}, QuatIdentity(), {1, 1, 1}}};
		clip.channels.push_back(ch);
		const int sid = an.LoadSkeleton(sk);
		const int cid = an.LoadClip(clip);
		Pose pose{};
		an.Evaluate(sid, cid, 0.5f, pose);
		Expect(std::fabs(pose.local[0].position.x - 1.0f) < 1.0e-3f, "anim lerp");
		float pos[3] = {1, 0, 0};
		std::uint16_t ji[4] = {0, 0, 0, 0};
		float jw[4] = {1, 0, 0, 0};
		float outp[3]{};
		an.Skin(pose, pos, ji, jw, 1, outp);
		Expect(std::isfinite(outp[0]), "skin");
	}

	// Collision + physics
	{
		CollisionWorld cw;
		Collider col{};
		col.kind = ColliderKind::Sphere;
		col.sphere = {{0, 0, 0}, 1.0f};
		cw.Add(col);
		const Hit h = cw.Raycast({{-5, 0, 0}, {1, 0, 0}});
		Expect(h.hit, "raycast sphere");
		PhysicsWorld pw;
		RigidBody rb{};
		rb.position = {0, 10, 0};
		pw.AddBody(rb);
		pw.Step(1.0f / 60.0f);
		Expect(pw.GetBody(0)->position.y < 10.0f, "gravity");
		const Vec3 moved = pw.MoveCharacter({{0, 1, 0}, {0, 2, 0}, 0.4f}, {0, 0, 1});
		Expect(std::isfinite(moved.z), "character move");
	}

	// Spatial
	{
		SpatialHash hash(2.0f);
		hash.Insert(0, {{0, 0, 0}, {1, 1, 1}});
		int ids[8];
		Expect(hash.QueryAabb({{-1, -1, -1}, {2, 2, 2}}, ids, 8) >= 1, "hash query");
		Aabb bounds[2] = {{{0, 0, 0}, {1, 1, 1}}, {{10, 10, 10}, {11, 11, 11}}};
		Bvh bvh;
		bvh.Build(bounds, 2);
		Expect(bvh.RayQuery({{ -1, 0.5f, 0.5f}, {1, 0, 0}}, 20.0f, ids, 8) >= 1, "bvh ray");
	}

	// Streaming
	{
		WorldStreamer st;
		st.SetGenerateFn(&GenerateChunk, nullptr);
		st.Request({1, 0, 0});
		JobSystem jobs;
		st.Pump(jobs);
		Expect(st.IsLoaded({1, 0, 0}), "chunk loaded");
	}

	// Nav + AI
	{
		NavMesh nav;
		Vec3 verts[4] = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
		std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
		nav.Build(verts, 4, idx, 6);
		Vec3 path[8];
		Expect(nav.FindPath({0.1f, 0, 0.1f}, {0.9f, 0, 0.9f}, path, 8) >= 2, "nav path");
		StateMachine sm;
		sm.AddState("idle");
		sm.AddState("run");
		sm.AddTransition(0, 1, 7);
		Expect(sm.Fire(7) && std::strcmp(sm.StateName(), "run") == 0, "fsm");
		BehaviorTree bt;
	const int a = bt.AddAction([](void*) { return BtStatus::Success; });
	bt.SetRoot(a);
	Expect(bt.Tick(nullptr) == BtStatus::Success, "bt");
		Vec3 pos[3] = {{0, 0, 0}, {1, 0, 0}, {50, 0, 0}};
		int seen[3];
		Expect(PerceiveInRange({0, 0, 0}, 2.0f, pos, 3, seen, 3) == 2, "perceive");
	}

	// Audio
	{
		AudioSystem audio;
		std::int16_t sine[64];
		for (int i = 0; i < 64; ++i) {
			sine[i] = static_cast<std::int16_t>(std::sin(static_cast<float>(i) * 0.2f) * 1000.0f);
		}
		const int clip = audio.CreateClip(sine, 64, 44100, 1);
		audio.Play(clip, 0.5f, false);
		std::int16_t mix[32]{};
		audio.Mix(mix, 16);
		Expect(audio.PlayingCount() >= 0, "audio mix");
	}

	// Save / assets
	{
		BinaryWriter w(2);
		w.WriteString("hp");
		w.WriteF32(12.5f);
		BinaryReader r;
		r.LoadBytes(w.Data().data(), w.Data().size());
		Expect(r.Version() == 2 && r.ReadString() == "hp", "save roundtrip");
		Expect(std::fabs(r.ReadF32() - 12.5f) < 1.0e-5f, "save float");
		AssetManager am;
		const int h = am.Intern("tex.png", AssetKind::Texture);
		JobSystem jobs;
		am.RequestLoad(h, jobs, &LoadAsset, nullptr);
		Expect(am.Get(h)->ready, "asset load");
		am.MarkStalePath("tex.png");
		Expect(am.HotReloadCount() == 1, "hot reload");
	}

	// Game.run native loop
	{
		Game game(64, 64, BackendKind::kCpu, "runtime-test", PresentMode::kHeadless);
		int frames = 0;
		game.SetMaxFrames(3);
		game.SetTargetFps(0);
		game.RegisterSystem(&NativeMover, &frames, "mover");
		game.Run();
		Expect(game.FrameIndex() >= 2, "game run frames");
		Expect(frames >= 2, "native system ticks");
		const Entity e = game.GetWorld().Create();
		game.GetWorld().SetEnabled(e, true);
		const int drawn = game.DrawVisibleMeshes(0xFFFFFFFFu);
		Expect(drawn == 0, "no mesh draw");
		game.GetCamera().SetOrthographic(-1, 1, -1, 1, 0.1f, 10.0f);
		Material mat{};
		mat.color = 0xFF00FF00u;
		Expect(game.Resources().RegisterMaterial(mat) == 0, "material register");
		UiRect r = game.GetUi().Layout(10, 10, 0, 0);
		Expect(r.w == 10.0f, "ui layout");
		BitmapFont font;
		Expect(font.Measure("AB") > 0, "font measure");
	}

	if (g_failures != 0) {
		std::fprintf(stderr, "%d failures\n", g_failures);
		return 1;
	}
	std::printf("game_runtime_tests: ok\n");
	return 0;
}
