#pragma once
#include "Kans3D/Scene/Entity.h"
namespace Kans
{
    class ScriptableEntity
    {
    public:
        virtual ~ScriptableEntity() = default;
        template<typename T>
        T& GetComponent()
        {
            return m_Entity.GetComponent<T>();
        }

        template<typename T, typename F>
        void PatchComponent(F&& edit)
        {
            m_Entity.PatchComponent<T>(std::forward<F>(edit));
        }
        template<typename T>
        void SetComponent(const T& value)
        {
            m_Entity.SetComponent<T>(value);
        }

    protected:
        virtual void OnUpdate(TimeStep ts) {}
        virtual void OnCreate() {}
        virtual void OnDestory() {}

    private:
        Entity m_Entity;
        friend class Scene;
    };

} // namespace Kans