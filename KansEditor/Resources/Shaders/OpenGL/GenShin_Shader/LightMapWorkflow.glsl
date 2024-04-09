//崩坏3原始算法
            //压缩三通道的Lightmap //顶点色的r通道
SWeight = LightMapColor.g * input.color.r;

float SFactor = 1.0f - step(0.5f, SWeight);
//float SFactor 1.0 - step(0.5f, SWeight);;
float2 halfFactor = SWeight * float2(1.2f, 1.25f) + float2(-0.1f, -0.125f);
SWeight = SFactor * halfFactor.x + (1.0f - SFactor) * halfFactor.y;
SWeight = floor((SWeight + input.lambert) * 0.5 + 1.0 - _ShadowArea);
SFactor = step(SWeight, 0);
ShadowColor.rgb = SFactor * baseColor.rgb + (1 - SFactor) * ShadowColor.rgb;