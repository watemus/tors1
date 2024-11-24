docker compose up --build -d
docker-compose exec worker1 iptables -A INPUT -m statistic --mode random --probability 1 -j DROP
sleep 5

docker compose logs
docker compose down -t 1
