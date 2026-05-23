import os
import requests
from com.google.transit.realtime import gtfs_realtime_pb2
from dataclasses import dataclass
from datetime import datetime
from zoneinfo import ZoneInfo

@dataclass
class TrainArrival:
    line: str
    arrival_time: datetime
    minutes_until_arrival: float
    int_minutes_until_arrival: int

class TrainFeed:
    FEED_URL = "https://api-endpoint.mta.info/Dataservice/mtagtfsfeeds/nyct%2Fgtfs-ace"
    TIMEZONE = ZoneInfo('America/New_York')
    TARGET_STOP_ID = os.getenv("MTA_TARGET_STOP_ID")
    TARGET_ROUTES = {"A", "C"}

    def get_feed_contents(self):
        response = requests.get(self.FEED_URL, timeout=10)
        print("Feed Response Code: " + str(response.status_code))
        response.raise_for_status()
        feed_message = gtfs_realtime_pb2.FeedMessage()
        feed_message.ParseFromString(response.content)
        return feed_message

    def parse_feed_contents(self, feed_message, current_time):
        entities = feed_message.entity
        upcoming_trains = []
        for mta_entity in entities:
            if not mta_entity.HasField("trip_update"):
                continue
            if mta_entity.trip_update.trip.route_id in self.TARGET_ROUTES:
                for stop_update_obj in mta_entity.trip_update.stop_time_update:
                    if not stop_update_obj.HasField("arrival"): 
                        continue
                    if stop_update_obj.stop_id == self.TARGET_STOP_ID:
                        arrival_time = datetime.fromtimestamp(stop_update_obj.arrival.time, tz=self.TIMEZONE)
                        time_delta_until_arrival = arrival_time - current_time
                        minutes_until_arrival = time_delta_until_arrival.total_seconds() / 60
                        int_minutes_until_arrival = int(time_delta_until_arrival.total_seconds() // 60)
                        line = mta_entity.trip_update.trip.route_id
                        if minutes_until_arrival >= 0:
                            upcoming_trains.append(TrainArrival(line, arrival_time, minutes_until_arrival, int_minutes_until_arrival))
        return upcoming_trains

    def print_sorted_arrival_times(self, upcoming_trains, current_time):
        sorted_upcoming_trains = sorted(upcoming_trains, key=lambda t: t.arrival_time)
        print("NOW: " + current_time.strftime('%Y-%m-%d %H:%M:%S') + "\n")
        for upcoming_train in sorted_upcoming_trains:
            print("Stop Update: ")
            print("Line: " + upcoming_train.line)
            print("Minutes until arrival: " + str(upcoming_train.minutes_until_arrival))
            print("Floor of Minutes until arrival: " + str(upcoming_train.int_minutes_until_arrival))
            print("ARRIVING AT: " + upcoming_train.arrival_time.strftime('%Y-%m-%d %H:%M:%S'))
            print("~~~~~~~~~~~~~\n")

    def get_upcoming_trains(self):
        feed_contents = self.get_feed_contents()
        current_time = datetime.now(self.TIMEZONE)
        upcoming_trains = self.parse_feed_contents(feed_contents, current_time)
        self.print_sorted_arrival_times(upcoming_trains, current_time)

if __name__ == "__main__":
    train_feed = TrainFeed()

    if train_feed.TARGET_STOP_ID is None:
        raise RuntimeError("Missing required env variable: MTA_TARGET_STOP_ID")

    train_feed.get_upcoming_trains()

