/* Copyright (C) 2012 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"

#include "ScenarioEditor/ScenarioEditor.h"
#include "Common/Tools.h"
#include "Common/Brushes.h"
#include "Common/MiscState.h"
#include "GameInterface/Messages.h"

using AtlasMessage::Position;

class ObjectBrush : public StateDrivenTool<ObjectBrush>
{
	DECLARE_DYNAMIC_CLASS(ObjectBrush);

	Position m_Pos;

	Brush m_ObjectBrush;

	// Brush settings:
	float m_Density;
	int m_ReplaceMode;
	bool m_RandomRotation;

public:
	ObjectBrush() : m_Density(1.0), m_ReplaceMode(AtlasMessage::eObjectBrushReplaceMode::NONE), m_RandomRotation(true)
	{
		SetState(&Waiting);

		m_ObjectBrush.SetCircle(4);
	}

	void SendObjectMsg(bool preview)
	{
		
		//if (preview)
			//POST_MESSAGE(ObjectPreview, ((std::wstring)g_SelectedObject.wc_str(), GetScenarioEditor().GetObjectSettings().GetSettings(), m_ObjPos, useTarget, m_Target, g_DefaultAngle, m_ActorSeed));
		//else
		{
			POST_COMMAND(ObjectBrush, ((std::wstring)g_SelectedObject.wc_str(), GetScenarioEditor().GetObjectSettings().GetSettings(), m_Pos, m_Density, m_RandomRotation, m_ReplaceMode));
		}
	}

	virtual void Init(void* initData, ScenarioEditor* scenarioEditor)
	{
		StateDrivenTool<ObjectBrush>::Init(initData, scenarioEditor);
	}

	void OnEnable()
	{
		m_ObjectBrush.MakeActive();
	}

	void OnDisable()
	{
		POST_MESSAGE(BrushPreview, (false, Position()));
	}

	struct sWaiting : public State
	{
		bool OnMouse(ObjectBrush* obj, wxMouseEvent& evt)
		{
			if (evt.LeftDown())
			{
				obj->m_Pos = Position(evt.GetPosition());
				SET_STATE(PlaceObjects);
				return true;
			}
			else if (evt.Moving())
			{
				POST_MESSAGE(BrushPreview, (true, Position(evt.GetPosition())));
				return true;
			}
			else
			{
				return false;
			}
		}
	}
	Waiting;


	struct sPlaceObjects : public State
	{
		void OnEnter(ObjectBrush* obj)
		{
			Place(obj);
		}

		void OnLeave(ObjectBrush*)
		{
			ScenarioEditor::GetCommandProc().FinaliseLastCommand();
		}

		bool OnMouse(ObjectBrush* obj, wxMouseEvent& evt)
		{
			if (evt.LeftUp())
			{
				SET_STATE(Waiting);
				return true;
			}
			else if (evt.Dragging())
			{
				wxPoint pos = evt.GetPosition();
				obj->m_Pos = Position(pos);
				Place(obj);
				return true;
			}
			else
			{
				return false;
			}
		}

		void Place(ObjectBrush* obj)
		{
			POST_MESSAGE(BrushPreview, (true, obj->m_Pos));
			obj->SendObjectMsg(false);
		}
	}
	PlaceObjects;
};

IMPLEMENT_DYNAMIC_CLASS(ObjectBrush, StateDrivenTool<ObjectBrush>);
