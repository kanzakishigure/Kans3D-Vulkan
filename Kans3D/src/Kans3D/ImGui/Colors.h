#pragma once
#include <imgui.h>
namespace Colors 
{
	//color scheme from  https://yeun.github.io/open-color/ingredients.html

	//color picking from unreal
	namespace Theme
	{
		//Gray
		constexpr auto gray_0	=	IM_COL32(248, 249, 250, 255);
		constexpr auto gray_1	=	IM_COL32(241, 243, 245, 255);
		constexpr auto gray_2	=	IM_COL32(233, 236, 239, 255);
		constexpr auto gray_3	=	IM_COL32(222, 226, 230, 255);
		constexpr auto gray_4	=	IM_COL32(206, 212, 218, 255);
		constexpr auto gray_5	=	IM_COL32(173, 181, 189, 255);
		constexpr auto gray_6	=	IM_COL32(134, 142, 150, 255);
		constexpr auto gray_7	=	IM_COL32(73, 80, 87, 255);
		constexpr auto gray_8   =   IM_COL32(52, 58, 64, 255);
		constexpr auto gray_9	=	IM_COL32(33, 37, 41, 255);
		constexpr auto gray_10  =   IM_COL32(13, 17, 18, 255);


		//Blue
		constexpr auto blue_0 = IM_COL32(231, 245, 255, 255);
		constexpr auto blue_1 = IM_COL32(208, 235, 255, 255);
		constexpr auto blue_2 = IM_COL32(165, 216, 255, 255);
		constexpr auto blue_3 = IM_COL32(116, 192, 252, 255);
		constexpr auto blue_4 = IM_COL32(77 , 171, 247, 255);
		constexpr auto blue_5 = IM_COL32(51 , 154, 240, 255);
		constexpr auto blue_6 = IM_COL32(34 , 139, 230, 255);
		constexpr auto blue_7 = IM_COL32(28 , 126, 214, 255);
		constexpr auto blue_8 = IM_COL32(25 , 113, 194, 255);
		constexpr auto blue_9 = IM_COL32(24 , 100, 171, 255);
		
		//Indigo 
		constexpr auto indigo_0 = IM_COL32(237, 242, 255, 255);
		constexpr auto indigo_1 = IM_COL32(219, 228, 255, 255);
		constexpr auto indigo_2 = IM_COL32(186, 200, 255, 255);
		constexpr auto indigo_3 = IM_COL32(145, 167, 255, 255);
		constexpr auto indigo_4 = IM_COL32(116, 143, 252, 255);
		constexpr auto indigo_5 = IM_COL32(92 , 124, 250, 255);
		constexpr auto indigo_6 = IM_COL32(76 , 110, 245, 255);
		constexpr auto indigo_7 = IM_COL32(66 , 99 , 235, 255);
		constexpr auto indigo_8 = IM_COL32(59 , 91 , 219, 255);
		constexpr auto indigo_9 = IM_COL32(54 , 79 , 199, 255);

		//Cyan 
		constexpr auto cyan_0 = IM_COL32(227, 250, 252, 255);
		constexpr auto cyan_1 = IM_COL32(197, 246, 250, 255);
		constexpr auto cyan_2 = IM_COL32(153, 233, 242, 255);
		constexpr auto cyan_3 = IM_COL32(102, 217, 232, 255);
		constexpr auto cyan_4 = IM_COL32(59 , 201, 219, 255);
		constexpr auto cyan_5 = IM_COL32(34 , 184, 207, 255);
		constexpr auto cyan_6 = IM_COL32(21 , 170, 191, 255);
		constexpr auto cyan_7 = IM_COL32(16 , 152, 173, 255);
		constexpr auto cyan_8 = IM_COL32(12 , 133, 153, 255);
		constexpr auto cyan_9 = IM_COL32(11 , 114, 133, 255);

		//Red
		constexpr auto red_0 = IM_COL32(255, 245, 245, 255);
		constexpr auto red_1 = IM_COL32(255, 227, 227, 255);
		constexpr auto red_2 = IM_COL32(255, 201, 201, 255);
		constexpr auto red_3 = IM_COL32(255, 168, 168, 255);
		constexpr auto red_4 = IM_COL32(255, 135, 135, 255);
		constexpr auto red_5 = IM_COL32(255, 107, 107, 255);
		constexpr auto red_6 = IM_COL32(250, 82 , 82 , 255);
		constexpr auto red_7 = IM_COL32(240, 62 , 62 , 255);
		constexpr auto red_8 = IM_COL32(224, 49 , 49 , 255);
		constexpr auto red_9 = IM_COL32(201, 42 , 42 , 255);

		//from unreal color picking
		//constexpr auto header = IM_COL32(47, 47, 47, 250);

		constexpr auto header          = gray_8;
		constexpr auto background      = IM_COL32(36, 36, 36, 255);
		constexpr auto backgroundDark  = IM_COL32(26, 26, 26, 255);
		constexpr auto backgroundPopup = gray_9;
		constexpr auto titlebar		   = IM_COL32(21, 21, 21, 255);
		
		constexpr auto propertyField   = gray_10;

		constexpr auto highlight	   = IM_COL32(39, 185, 242, 255);;

		
		constexpr auto text = gray_2;
	}
}
