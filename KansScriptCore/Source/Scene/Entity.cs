namespace Kans
{
    public class Entity
    {
        public ulong ID { get; private set; }
        private TransformComponent m_Transform;
        private MaterialComponent m_Material;
        private CameraComponent m_Camera;
        private ScriptComponent m_Script;

        public Entity() : this(0) { }
        public Entity(ulong id)
        {
            ID = id;
            // Wrappers are created before callbacks, including for absent components.
            m_Transform = new TransformComponent { Entity = this };
            m_Material = new MaterialComponent { Entity = this };
            m_Camera = new CameraComponent { Entity = this };
            m_Script = new ScriptComponent { Entity = this };
        }

        protected virtual void OnCreate() { }
        protected virtual void OnUpdate(float ts) { }
        protected virtual void OnDestroy() { }
        internal void Attach(ulong id) { ID = id; }
        internal void Create() => OnCreate();
        internal void Update(float ts) => OnUpdate(ts);
        internal void Destroy() => OnDestroy();

        public bool HasComponent<T>() => InternalCalls.HasComponent(ID, ComponentType<T>.ID);
        public T? GetComponent<T>() where T : Component, new()
        {
            if (!HasComponent<T>()) return null;
            // These casts do not box or allocate; no reflection or generic Activator on the frame path.
            if (typeof(T) == typeof(TransformComponent)) return (T)(Component)m_Transform;
            if (typeof(T) == typeof(MaterialComponent)) return (T)(Component)m_Material;
            if (typeof(T) == typeof(CameraComponent)) return (T)(Component)m_Camera;
            if (typeof(T) == typeof(ScriptComponent)) return (T)(Component)m_Script;
            return null;
        }
    }

    internal static class ComponentType<T>
    {
        internal static readonly uint ID = typeof(T) == typeof(TransformComponent) ? ComponentIds.TransformComponent :
            typeof(T) == typeof(MaterialComponent) ? ComponentIds.MaterialComponent :
            typeof(T) == typeof(CameraComponent) ? ComponentIds.CameraComponent :
            typeof(T) == typeof(ScriptComponent) ? ComponentIds.ScriptComponent : 0;
    }
}
