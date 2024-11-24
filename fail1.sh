docker compose up --build -d

sleep 1

docker compose logs

docker compose stop worker1 -t 0
docker compose stop worker2 -t 0

sleep 2

docker compose logs

sleep 2

docker compose logs
docker compose down -t 1
