using System;
using System.Diagnostics.PerformanceData;
using System.Runtime.CompilerServices;
using static Kans.Input;
using static System.Runtime.CompilerServices.RuntimeHelpers;

namespace Kans
{
    public struct Vector3
    {

        static Vector3 Zero => new Vector3(0.0f);
        public float X;
        public float Y;
        public float Z;
        public Vector3(float x,float y,float z)
        {
            X=  x;
            Y = y;
            Z = z;
        }
        public Vector3(float Scale)
        {
            X = Scale;
            Y = Scale;
            Z = Scale;
        }
        public static Vector3 operator* (Vector3 vector,float scale)
        {
            return new Vector3(vector.X * scale, vector.Y * scale, vector.Z * scale);
        }
       
    }
    public class Entity
    {
        protected ulong ID;

        public Vector3 Translation
        {
            get 
            {
                InternalCalls.EntityGetTranslation(ID, out Vector3 tranlation);
                return tranlation;
            }
            set 
            {
                InternalCalls.EntitySetTranslation(ID,ref value );
            }
        }
        public Entity()
        {

        }
        public Entity(ulong id)
        {
            ID = id;
        }


        void OnCreate()
        {
            
        }
        void OnUpdate(float ts)
        {

        }
    }
    
   

    public class Component
    {

    }
    public  class Transform : Component
    {

    }

    


}
