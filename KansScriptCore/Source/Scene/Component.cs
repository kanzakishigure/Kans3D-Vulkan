using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Kans
{
    public class Component
    {
        public Entity Entity { get; internal set; }
    }
    public class TransformComponent : Component
    {
        public Vector3 Translation
        {
            get
            {
                InternalCalls.Entity_GetTranslation(Entity.ID, out Vector3 tranlation);
                return tranlation;
            }
            set
            {
                
                InternalCalls.Entity_SetTranslation(Entity.ID, ref value);
            }
        }
    }

    public class MaterialComponent : Component
    {
        
    }
 
}
