FROM alpine:latest as builder
RUN apk update && apk add --no-cache gcc make musl-dev

COPY . .
RUN make && make install

FROM alpine:latest as runner

COPY --from=builder /usr/local/bin/jdupes /usr/local/bin/jdupes

ENTRYPOINT [ "/usr/local/bin/jdupes" ]
