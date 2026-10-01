# /// script
# requires-python = ">=3.10"
# dependencies = ["google-analytics-data"]
# ///
"""Print GA4 sessions/users for the Infinite Website property as JSON. Run by roadmap.py via `uv run`.

Auth: the ga4-reader service account (Viewer on the property). The key file is found at run time from
$GOOGLE_APPLICATION_CREDENTIALS or the first *.json in ~/.config/ga4-reader; this file never prints it.
"""
import glob
import json
import os
import sys

PROPERTY = os.environ.get("GA4_PROPERTY", "556148687")
days = int(sys.argv[1]) if len(sys.argv) > 1 else 30

if not os.environ.get("GOOGLE_APPLICATION_CREDENTIALS"):
    keys = sorted(glob.glob(os.path.expanduser("~/.config/ga4-reader/*.json")))
    if not keys:
        sys.exit("no service-account key: set GOOGLE_APPLICATION_CREDENTIALS")
    os.environ["GOOGLE_APPLICATION_CREDENTIALS"] = keys[0]

from google.analytics.data_v1beta import BetaAnalyticsDataClient
from google.analytics.data_v1beta.types import DateRange, Dimension, Metric, RunReportRequest

c = BetaAnalyticsDataClient()
who = getattr(c.transport._credentials, "service_account_email", "?")  # an email address, not a secret
rng = [DateRange(start_date=f"{days}daysAgo", end_date="today")]
try:
    tot = c.run_report(RunReportRequest(property=f"properties/{PROPERTY}", date_ranges=rng,
                                    metrics=[Metric(name="sessions"), Metric(name="totalUsers")]))
except Exception as e:
    sys.exit(f"{type(e).__name__} as {who} on properties/{PROPERTY}: {str(e)[:120]}")
row = tot.rows[0].metric_values if tot.rows else None
daily = c.run_report(RunReportRequest(property=f"properties/{PROPERTY}", date_ranges=rng,
                                      dimensions=[Dimension(name="date")], metrics=[Metric(name="sessions")]))
ev = c.run_report(RunReportRequest(property=f"properties/{PROPERTY}", date_ranges=rng,
    dimensions=[Dimension(name="customEvent:os")], metrics=[Metric(name="eventCount")],
    dimension_filter={"filter": {"field_name": "eventName", "string_filter": {"value": "download_click"}}}))
dl_os = {r.dimension_values[0].value: int(r.metric_values[0].value) for r in ev.rows}
print(json.dumps({"dl": sum(dl_os.values()), "dl_os": dl_os, "sessions": int(row[0].value) if row else 0, "users": int(row[1].value) if row else 0, "days": days,
                  "daily": sorted((r.dimension_values[0].value, int(r.metric_values[0].value)) for r in daily.rows)}))
