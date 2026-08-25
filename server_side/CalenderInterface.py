from ics import Calendar
from dotenv import load_dotenv
import requests, arrow, os
import arrow

load_dotenv()

url =  os.environ["CALENDER_URL"]
resp = requests.get(url)
cal = Calendar(resp.text)

now = arrow.utcnow()
upcoming = sorted(
    (e for e in cal.events if e.begin > now),
    key=lambda e: e.begin
)
next_event = upcoming[0] if upcoming else None
if next_event:
    print(next_event.name, next_event.begin)