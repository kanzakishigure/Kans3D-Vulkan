namespace Kans
{
    public class Component
    {
        public Entity Entity { get; internal set; } = null!;
    }
    public class TransformComponent : Component
    {
        public Vector3 Translation
        {
            get => InternalCalls.GetTranslation(Entity.ID);
            set => InternalCalls.SetTranslation(Entity.ID, value);
        }
        public Vector3 Rotation
        {
            get => InternalCalls.GetRotation(Entity.ID);
            set => InternalCalls.SetRotation(Entity.ID, value);
        }
    }
    public class MaterialComponent : Component { }
    public class CameraComponent : Component { }
    public class ScriptComponent : Component { }
}
