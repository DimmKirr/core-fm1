# SPDX-License-Identifier: GPL-3.0-only
from unittest.mock import MagicMock

import pytest

from fm1_controls import FM1Controls


def test_held_button_released_on_action_failure():
    page = MagicMock()
    with pytest.raises(RuntimeError, match='action failed'):
        with FM1Controls(page).hold_button('EDIT', delay_ms=0):
            raise RuntimeError('action failed')
    page.keyboard.down.assert_called_once_with('5')
    page.keyboard.up.assert_called_once_with('5')


@pytest.mark.parametrize('method,args,kwargs', [
    ('rotate_encoder', ('missing',), {}),
    ('rotate_encoder', ('select',), {'direction': 0}),
    ('rotate_encoder', ('select',), {'steps': -1}),
    ('send_encoder', (7, 1), {}),
    ('press_button', ('missing',), {}),
    ('play_note', ('C9',), {}),
])
def test_invalid_control_does_not_send_input(method, args, kwargs):
    page = MagicMock()
    with pytest.raises(ValueError):
        getattr(FM1Controls(page), method)(*args, **kwargs)
    assert not page.mock_calls
