using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Kans
{
    public struct Vector3
    {

        static Vector3 Zero => new Vector3(0.0f);
        public float X;
        public float Y;
        public float Z;
        public Vector3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }
        public Vector3(float Scale)
        {
            X = Scale;
            Y = Scale;
            Z = Scale;
        }
        public static Vector3 operator *(Vector3 vector, float scale)
        {
            return new Vector3(vector.X * scale, vector.Y * scale, vector.Z * scale);
        }

        public static Vector3 operator +(Vector3 v1, Vector3 v2)
        {
            return new Vector3(v1.X+ v2.X, v1.Y + v2.Y, v1.Z + v2.Z);
        }

    }
}
