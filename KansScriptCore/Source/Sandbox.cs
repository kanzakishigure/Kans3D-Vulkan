using System;
using Kans;

namespace Sandbox
{
    public class Player : Entity
    {
        public float MyPublicFloatVar = 5.0f;
        public Vector3 m_move;
        public void PrintFloatVar()
        {

            Console.WriteLine("MyPublicFloatVar = {0:F}", MyPublicFloatVar);

            Vector3 v1 = new Vector3(1, 0, 0);
            InternalCalls.NativeLog(ref v1);

            Vector3 v2 = new Vector3(0, 1, 0);
            InternalCalls.NativeLog(ref v2);

            Console.WriteLine($"{InternalCalls.VectorDot(ref v1, ref v2).ToString()}");


        }

        private void IncrementFloatVar(float value)
        {
            MyPublicFloatVar += value;
        }

        private void Printstring(string name, string value)
        {
            Console.WriteLine($"{name}: call function with value: {value}");
        }

        protected override void OnCreate()
        {
            Console.WriteLine($"call OnCreate,m id is {ID}");
            Random random = new Random();


            m_move = new Vector3((float)random.NextDouble(), (float)random.NextDouble(), (float)random.NextDouble());
        }
        protected override void OnUpdate(float ts)
        {
            // Console.WriteLine("call OnUpdate");
            if(HasComponent<TransformComponent>())
            {
                Vector3 position =  GetComponent<TransformComponent>().Translation;
                Vector3 rotation = GetComponent<TransformComponent>().Rotation;
                position += m_move* ts;



                if (Input.IsKeyPressed(KeyCode.W))
                        position.Y += ts;

                    if (Input.IsKeyPressed(KeyCode.S))
                        position.Y -= ts;

                    if (Input.IsKeyPressed(KeyCode.A))
                        position.X -= ts;

                    if (Input.IsKeyPressed(KeyCode.D))

                        position.X += ts;

                    if (Input.IsKeyPressed(KeyCode.Q))
                    {
                        rotation.X += ts;

                        GetComponent<TransformComponent>().Rotation = rotation;
                    }
                        
                    if (Input.IsKeyPressed(KeyCode.E))
                    {
                        rotation.Y += ts;
                       GetComponent<TransformComponent>().Rotation = rotation;
                    }
                       
                GetComponent<TransformComponent>().Translation = position;
               
            }
           
            
        }

    }

}

