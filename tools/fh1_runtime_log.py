"""Select one session from chronologically merged FH1 runtime logs."""
import json


def session_lines(lines, session):
    if not session:
        raise ValueError('an explicit session is required')
    active = None
    found = False
    for line in lines:
        if 'M2_EVENT ' in line:
            event = json.loads(line.split('M2_EVENT ', 1)[1])
            if event.get('event') == 'logging.ready':
                active = event['session']
                found |= active == session
        if active == session:
            yield line
    if not found:
        raise ValueError('session start missing; check log rotation or session id')
