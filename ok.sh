docker compose up --build -d

sleep 5

docker compose logs
docker compose down -t 1
