#include "kspch.h"
#include "LayerStack.h"

Kans::LayerStack::LayerStack()
{
	
}

Kans::LayerStack::~LayerStack()
{
	
}

void Kans::LayerStack::PushLayer(Layer* layer)
{
	m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);//��������֮ǰ����һ��Ԫ�أ��������²���Ԫ�صĵ�����
	//��֤overlayer��Զ��layer����Ⱦ
	m_LayerInsertIndex++;
}

void Kans::LayerStack::PushOverlay(Layer* overlay)
{
	m_Layers.emplace_back(overlay);//��ջβѹ��һ��Layer,ʵ�ֽ������push_back,������stlԴ����棬emplaceЧ�ʸ���
}

void Kans::LayerStack::PopLayer(Layer* layer)
{
	auto it = std::find(m_Layers.begin(), m_Layers.end(), layer);//���Ҵ����layer
	//��find�����ҵ��ˣ���᷵�ض�Ӧ��������û�ҵ���᷵��һ���յ�������
	if (it != m_Layers.end())//������ҵ���
	{
		layer->OnDetach();
		m_Layers.erase(it);
		m_LayerInsertIndex--;
	}
}

void Kans::LayerStack::PopOverlay(Layer* overlay)
{
	auto it = std::find(m_Layers.begin(), m_Layers.end(), overlay);//���Ҵ����layer
	
	if (it != m_Layers.end())
	{
		overlay->OnDetach();
		m_Layers.erase(it);
	}
}
