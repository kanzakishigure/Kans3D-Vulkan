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
	m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);//在索引器之前插入一个元素，并返回新插入元素的迭代器
	//保证overlayer永远比layer后渲染
	m_LayerInsertIndex++;
}

void Kans::LayerStack::PushOverlay(Layer* overlay)
{
	m_Layers.emplace_back(overlay);//在栈尾压入一层Layer,实现结果类似push_back,但是在stl源码层面，emplace效率更高
}

void Kans::LayerStack::PopLayer(Layer* layer)
{
	auto it = std::find(m_Layers.begin(), m_Layers.end(), layer);//查找传入的layer
	//当find函数找到了，则会返回对应迭代器，没找到则会返回一个空迭代器？
	if (it != m_Layers.end())//如果查找到了
	{
		layer->OnDetach();
		m_Layers.erase(it);
		m_LayerInsertIndex--;
	}
}

void Kans::LayerStack::PopOverlay(Layer* overlay)
{
	auto it = std::find(m_Layers.begin(), m_Layers.end(), overlay);//查找传入的layer
	
	if (it != m_Layers.end())
	{
		overlay->OnDetach();
		m_Layers.erase(it);
	}
}
