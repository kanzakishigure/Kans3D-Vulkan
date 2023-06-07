float warp(float x, float w) {
	return (x + w) / (1 + w);
}

vec3 warp(vec3 x, vec3 w) {
	return (x + w) / (1 + w);
}
float Normal2ndc(float x) {
	return x * 2.0 - 1.0;
}
vec4 LightingToon(ToonSurfaceOutput s, vec3 lightDir, vec3 viewDir, float atten) {
			// Array
			vec3 nNormal = normalize(s.Normal);
			vec3 HDir = normalize(lightDir + viewDir);

			float NoL = dot(nNormal, lightDir);
			float NoH = dot(nNormal, HDir);
			float NoV = dot(nNormal, viewDir);
			float VoL = dot(viewDir, lightDir);
			float VoH = dot(viewDir, HDir);

			float roughness = 0.95 - 0.95 * (s.smoothMap * _Glossiness);
			float _BoundSharp = 9.5 * Pow2(roughness - 1) + 0.5;
			//--------------------------------------------
			// Specular
			//--------------------------------------------

			float NDF0 = D_GGX(roughness * roughness, 1);
			float NDF_HBound = NDF0 * _DividLineSpec;

			float NDF = D_GGX(roughness * roughness, NoH) + _ShadowAttWeight * (atten - 1);
			float specularWin = sigmoid(NDF, NDF_HBound, _BoundSharp * _DividSharpness);

			float SpecWeight = specularWin * (NDF0 + NDF_HBound) / 2 * s.specIntensity; //optional

			//----------------------------------------------------
			// diffuse
			//--------------------------------------------
			float Lambert = NoL + _AOWeight * Normal2ndc(s.AO) + _ShadowAttWeight * (atten - 1);

			float MidSig = sigmoid(Lambert, _DividLineM, _BoundSharp * _DividSharpness);
			float DarkSig = sigmoid(Lambert, _DividLineD, _BoundSharp * _DividSharpness);

			float MidLWin = MidSig;
			float MidDWin = DarkSig - MidSig;
			float DarkWin = 1 - DarkSig;

			vec3 diffuseWeight = (MidLWin * (1 + ndc2Normal(_DividLineM)) / 2).xxx;
			diffuseWeight += (MidDWin * (ndc2Normal(_DividLineM) + ndc2Normal(_DividLineD)) / 2).xxx * _DarkFaceColor.rgb * 3 / (_DarkFaceColor.r + _DarkFaceColor.g + _DarkFaceColor.b);
			diffuseWeight += (DarkWin * (ndc2Normal(_DividLineD))).xxx * _DeepDarkColor.rgb * 3 / (_DeepDarkColor.r + _DeepDarkColor.g + _DeepDarkColor.b);
			diffuseWeight = warp(diffuseWeight, _diffuseBright.xxx);

			vec3 lightResult = SpecWeight * _LightColor0.rgb + (1 - SpecWeight) * diffuseWeight * s.diffColor.rgb;

			return half4(lightResult.rgb, 1.0);
		}