import requests
from com.google.transit.realtime import gtfs_realtime_pb2
from com.google.transit.realtime import gtfs_realtime_NYCT_pb2
from datetime import *
import zoneinfo

request = requests.get('https://api-endpoint.mta.info/Dataservice/mtagtfsfeeds/nyct%2Fgtfs-ace')
print(request.status_code)

feedMessage = gtfs_realtime_pb2.FeedMessage()
feedMessage.ParseFromString(request.content)

print(feedMessage.header.timestamp)
print(datetime.fromtimestamp(feedMessage.header.timestamp, tz=zoneinfo.ZoneInfo('America/New_York')).strftime('%Y-%m-%d %H:%M:%S'))

