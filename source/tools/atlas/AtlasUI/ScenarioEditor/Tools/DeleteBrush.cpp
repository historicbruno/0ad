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

class DeleteBrush : public StateDrivenTool<DeleteBrush>
{
	DECLARE_DYNAMIC_CLASS(DeleteBrush);

	Position m_Pos;

	Brush m_DeleteBrush;

	// Brush settings:
	float m_Density;
	int m_ReplaceMode;
	bool m_RandomRotation;

public:
	DeleteBrush()
	{
		SetState(&Waiting);

		m_DeleteBrush.SetCircle(4);
	}

	void OnEnable()
	{
		m_DeleteBrush.MakeActive();
	}

	void OnDisable()
	{
		POST_MESSAGE(BrushPreview, (false, Position()));
	}

	struct sWaiting : public State
	{
		bool OnMouse(DeleteBrush* obj, wxMouseEvent& evt)
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
		void OnEnter(DeleteBrush* obj)
		{
			Place(obj);
		}

		void OnLeave(DeleteBrush*)
		{
			ScenarioEditor::GetCommandProc().FinaliseLastCommand();
		}

		bool OnMouse(DeleteBrush* obj, wxMouseEvent& evt)
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

		void Place(DeleteBrush* obj)
		{
			POST_MESSAGE(BrushPreview, (true, obj->m_Pos));
			//POST_COMMAND(DeleteBrush, (obj->m_Pos, (std::wstring)g_SelectedTexture.wc_str(), GetPriority()));
		}
	}
	PlaceObjects;
};

IMPLEMENT_DYNAMIC_CLASS(DeleteBrush, StateDrivenTool<DeleteBrush>);
