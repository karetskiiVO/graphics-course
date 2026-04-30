module;

#include <concepts>
#include <string>
#include <vector>
#include <typeindex>
#include <format>
#include <stdexcept>
#include <chrono>
#include <span>
#include <ranges>

export module Engine;

template <std::ranges::range R, typename F>
void Each(R&& range, const F& f) requires std::invocable<F, std::ranges::range_value_t<R>> {
  for (auto& elem : range) std::invoke(f, elem);
}

export class World {
public:
    class Entity {
    public:
        class Component {
        public:
            Entity& GetOwner() { return *owner; }
            World&  GetWorld() { return *owner->GetWorld(); }

            virtual void Awake() {}
            virtual void Start() {}
            virtual void Update() {}
            virtual void LateUpdate() {}
            virtual void PreRender() {}
            virtual void Dispose() {}
            virtual ~Component() {}

            friend class Entity;
        private:
            // methods for chaining
            void EntityUpdate() {
                if (started) return;
                started = true;
                Start();
            }
            void EntityLateUpdate() { LateUpdate(); }
            void EntityPreRender()  { PreRender(); }
            void EntityDispose()    { Dispose(); }

            bool started = false;
            Entity* owner = nullptr;
        };

        template<std::derived_from<Component> Comp>
        Comp* GetComponent() {
            Comp* res = nullptr;
            for (auto component : components) {
                res = dynamic_cast<Comp*>(component);
                if (res != nullptr) break;
            }

            return res;
        }

        template<std::derived_from<Component> Comp>
        Comp* AddComponent() {
            Comp* res = new Comp();
            res->owner = this;
            components.push_back(res);
            res->Awake();
            return res;
        }

        template<std::derived_from<Component> Comp>
        void RemoveComponent() {
        for (auto it = components.begin(); it != components.end(); it++) {
            if (dynamic_cast<Comp*>(*it) != nullptr) {
                auto comp = dynamic_cast<Comp*>(*it);
                components.erase(it);
                comp->Dispose();
                delete comp;
                break;
            }
        }
        }

        World* GetWorld() const { return world; }

        const std::string& GetName() const { return name; }
        Entity& SetName (const std::string& New) {
            world->ChangeEntityName(name, New);
            this->name = New;
            return *this;
        }

        void Update()     { Each(components, &Component::EntityUpdate); }
        void LateUpdate() { Each(components, &Component::LateUpdate); }
        void PreRender()  { Each(components, &Component::PreRender); }

        ~Entity() {
            Each(components, &Component::EntityDispose);
            Each(components, [](Component* comp) { delete comp; });
        }

        friend class World;
    private:
        World* world = nullptr;
        bool initialized = false;
        std::string name;
        std::vector<Component*> components;
    };

    class System {
    public:
        virtual void Awake() {}
        virtual void Start() {}
        virtual void Update() {}
        virtual void LateUpdate() {}
        virtual void PreRender() {}
        virtual void Render() {}
        virtual void Dispose() {}
        virtual ~System() {}

        World& GetWorld() { return *world; }

        friend class World;
    private:
        void WorldUpdate() {
            if (started) return;
            started = true;
            Start();
        }
        void WorldLateUpdate() { LateUpdate(); }
        void WorldPreRender()  { PreRender(); }
        void WorldRender()     { Render(); }
        void WorldDispose()    { Dispose(); }

        bool started = false;
        World* world = nullptr;
    };

    World& ChangeSystemName(const std::string& /*old*/, const std::string& /*New*/) {
        throw std::runtime_error("UpdateSystemName: not implemented");
    }

    World& ChangeEntityName(const std::string& /*old*/, const std::string& /*New*/) {
        throw std::runtime_error("UpdateEntityName: not implemented");
    }

    Entity& AddEntity(const std::string& name) {
        auto res = new Entity();
        res->name = name;
        res->world = this;
        entities.push_back(res);

        return *res;
    }

    Entity& AddEntity() {
        auto creationCount = entityCreationCount++;
        auto name = std::format("entity_{}", creationCount);
        auto cnt = 0;

        auto contains = [&] (const std::string& possibleName) {
            return std::ranges::find_if(entities,
                [&] (Entity* entity) { return entity->name == possibleName; }
            ) != entities.end();
        };

        while (contains(name)) name = std::format("entity_{}_{}", creationCount, cnt++);

        return AddEntity(name);
    }

    void RemoveEntity(const std::string& name) {
        auto it = std::ranges::find_if(entities,
        [&](Entity* entity) { return entity->name == name; }
        );

        if (it != entities.end()) entities.erase(it);
    }

    void RemoveEntity(const Entity* entity) {
        auto it = std::ranges::find(entities, entity);
        if (it != entities.end()) entities.erase(it);
    }

    template<std::derived_from<System> Sys>
    Sys& AddSystem() {
        Sys* res = GetSystem<Sys>();
        if (res == nullptr) {
        res = new Sys();
        res->world = this;
        systems.push_back(res);
        res->Awake();
        }
        return *res;
    }

    Entity* GetEntity(const std::string& name) {
        Entity* res = nullptr;
        for (const auto entity : entities) {
        if (entity->name == name) {
            res = entity;
            break;
        }
        }

        return res;
    }

    const std::vector<Entity*>& GetEntities() const {
        return entities;
    }

    template<std::derived_from<System> Sys>
    Sys* GetSystem() {
        Sys* res = nullptr;
        for (auto system : systems) {
        res = dynamic_cast<Sys*>(system);
        if (res != nullptr) break;
        }

        return res;
    }

    void Run() {
        while (true) {
            Each(systems, &System::WorldUpdate);
            Each(entities, &Entity::Update);
            Each(systems, &System::WorldLateUpdate);
            Each(entities, &Entity::LateUpdate);
            Each(systems, &System::WorldPreRender);
            Each(entities, &Entity::PreRender);
            Each(systems, &System::WorldRender);
        }
    }

    ~World() {
        Each(systems, &System::Dispose);
        Each(systems, [](auto system) { delete system; });
        Each(entities, [](auto entity) { delete entity; });
    }
private:
    size_t entityCreationCount = 0;

    // TODO: приоритеты вызова
    std::vector<Entity*> entities;
    std::vector<System*> systems;
};

export class TimeSystem : public World::System {
public:
    void Awake() override {
        lastTime = std::chrono::high_resolution_clock::now();
    }

    void PreRender() override {
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> diff = now - lastTime;
        deltaTime = diff.count();
        totalTime += deltaTime;
        lastTime = now;
    }
    
    float Time()      const { return totalTime; }
    float DeltaTime() const { return deltaTime; }
private:
    float deltaTime = 0.0001f;
    float totalTime = 0.0f;
    
    std::chrono::time_point<std::chrono::high_resolution_clock> lastTime;
};

