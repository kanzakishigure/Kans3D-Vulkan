using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Kans
{
    public class Entity
    {
        public readonly ulong ID;
        private Dictionary<Type, Component> m_ComponentCache = new Dictionary<Type, Component>();

        public Entity()
        {

        }
        public Entity(ulong id)
        {
            ID = id;
        }


        protected virtual void OnCreate() {  }

        protected virtual void OnUpdate(float ts) {  }
        public bool HasComponent<T>()
        {
            return InternalCalls.Entity_HasComponent(ID, typeof(T));
        }

        public T GetComponent<T>() where T : Component, new()
        {
            if (HasComponent<T>())
            {
                Type componentType = typeof(T);
                if (!m_ComponentCache.ContainsKey(componentType))
                {
                    T component = new T { Entity = this };
                    m_ComponentCache[componentType] = component;
                    return component;
                }
                else
                {
                    return m_ComponentCache[componentType] as T;
                }
             
            }
            else
            {
                return null;
            }

        }
    }
}
