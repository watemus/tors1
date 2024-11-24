docker compose up --build -d

sleep 1

docker compose logs

docker compose stop worker1 -t 0

sleep 1

docker compose start worker1

sleep 5

docker compose logs
docker compose down -t 1
