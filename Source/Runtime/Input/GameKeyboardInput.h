#pragma once

#include "Input/InputSystem.h"

// PIE and standalone play must forward the same movement keys, including releases.
// Escape is handled separately by each host's play-session controls.
template <typename TReceiver>
void DispatchGameKeyboardInput(TReceiver&& Receive)
{
	constexpr EKeyCode Keys[] = {
		EKeyCode::W, EKeyCode::A, EKeyCode::S, EKeyCode::D,
		EKeyCode::Q, EKeyCode::E, EKeyCode::Space, EKeyCode::Shift,
		EKeyCode::C, EKeyCode::R,
		// 사진 모드: 조리개(↑↓), 셔터(←→), 줌(Z/X), AF 토글(F), AE 토글(G), 하프 프레임(H)
		EKeyCode::Up, EKeyCode::Down, EKeyCode::Left, EKeyCode::Right,
		EKeyCode::Z, EKeyCode::X, EKeyCode::F, EKeyCode::G, EKeyCode::H
	};
	for (EKeyCode Key : Keys)
	{
		if (FInputSystem::IsKeyPressed(Key)) Receive(static_cast<int32>(Key), true);
		if (FInputSystem::IsKeyReleased(Key)) Receive(static_cast<int32>(Key), false);
	}
}
