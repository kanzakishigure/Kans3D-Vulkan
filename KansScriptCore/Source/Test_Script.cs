using System;

namespace Kans
{
    public class Main
    {
        public float MyPublicFloatVar = 5.0f;

        public void PrintFloatVar()
        {
            Console.WriteLine("MyPublicFloatVar = {0:F}", MyPublicFloatVar);
        }

        private void IncrementFloatVar(float value)
        {
            MyPublicFloatVar += value;
        }

        private void Printstring(string name,string value)
        {
            Console.WriteLine($"{name}: call function with value: {value}");
        }

        private void Update()
        {

        }

        private void Awake()
        {

        }
    }
}
