using System;
using System.Text;

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
        internal void DestroyScript() => OnDestroy();

        /// <summary>Sets a runtime shader parameter on this rendered instance. Set it on the model root;
        /// it applies to all draws of that instance root, without changing the shared material.
        /// Scalar values use (value, 0, 0, 0). Names must fit in 255 UTF-8 bytes.</summary>
        public void SetMaterialParameter(string name, float value) => SetMaterialParameter(name, new Vector4(value, 0, 0, 0));

        /// <summary>Sets a runtime parameter on the model root's draws. Vector3 uses w = 0.</summary>
        public void SetMaterialParameter(string name, Vector3 value) => SetMaterialParameter(name, new Vector4(value.X, value.Y, value.Z, 0));

        /// <summary>Sets a runtime parameter on the model root's draws. Undeclared material names are ignored.</summary>
        public unsafe void SetMaterialParameter(string name, Vector4 value)
        {
            byte* bytes = stackalloc byte[256];
            EncodeParameterName(name, bytes);
            InternalCalls.Api.SetMaterialParameter(ID, bytes, &value.X);
        }

        /// <summary>Removes a runtime override, restoring its binding or removing the instance parameter.</summary>
        public unsafe void ClearMaterialParameter(string name)
        {
            byte* bytes = stackalloc byte[256];
            EncodeParameterName(name, bytes);
            InternalCalls.Api.ClearMaterialParameter(ID, bytes);
        }

        /// <summary>Reads the current effective binding or override on this model root. Returns false and a zero vector when absent.</summary>
        public unsafe bool TryGetMaterialParameter(string name, out Vector4 value)
        {
            byte* bytes = stackalloc byte[256];
            EncodeParameterName(name, bytes);
            Vector4 result = default;
            bool found = InternalCalls.Api.GetMaterialParameter(ID, bytes, &result.X) != 0;
            value = result;
            return found;
        }

        private static unsafe void EncodeParameterName(string name, byte* bytes)
        {
            ArgumentNullException.ThrowIfNull(name);
            int length = Encoding.UTF8.GetByteCount(name);
            if (length > 255 || name.AsSpan().IndexOf('\0') >= 0)
                throw new ArgumentException("Parameter names must fit in 255 UTF-8 bytes and contain no NUL.", nameof(name));
            Encoding.UTF8.GetBytes(name.AsSpan(), new Span<byte>(bytes, length));
            bytes[length] = 0;
        }

        public bool HasComponent<T>() => InternalCalls.HasComponent(ID, ComponentType<T>.ID);
        public T? GetComponent<T>() where T : Component, new()
        {
            if (!HasComponent<T>()) return null;
            // These casts do not box or allocate; no reflection or generic Activator on the frame path.
            if (typeof(T) == typeof(TransformComponent)) return (T)(Component)m_Transform;
            if (typeof(T) == typeof(MaterialComponent)) return (T)(Component)m_Material;
            if (typeof(T) == typeof(CameraComponent)) return (T)(Component)m_Camera;
            if (typeof(T) == typeof(ScriptComponent)) return (T)(Component)m_Script;
            return new T { Entity = this };
        }
        public unsafe T AddComponent<T>() where T : Component, new()
        {
            if (InternalCalls.Api.AddComponent(ID, ComponentType<T>.ID) <= 0)
                throw new InvalidOperationException($"Cannot add {typeof(T).Name} to entity {ID}.");
            return GetComponent<T>()!;
        }
        public unsafe void Destroy() => InternalCalls.Api.DestroyEntity(ID);
    }

    internal static class ComponentType<T>
    {
        internal static readonly uint ID = typeof(T) == typeof(TransformComponent) ? ComponentIds.TransformComponent :
            typeof(T) == typeof(MaterialComponent) ? ComponentIds.MaterialComponent :
            typeof(T) == typeof(CameraComponent) ? ComponentIds.CameraComponent :
            typeof(T) == typeof(ScriptComponent) ? ComponentIds.ScriptComponent :
            typeof(T) == typeof(TextComponent) ? ComponentIds.TextComponent :
            typeof(T) == typeof(SpriteRendererComponent) ? ComponentIds.SpriteRendererComponent :
            typeof(T) == typeof(UIScreenComponent) ? ComponentIds.UIScreenComponent :
            typeof(T) == typeof(UIWorldComponent) ? ComponentIds.UIWorldComponent :
            0;
    }
}
