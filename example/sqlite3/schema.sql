CREATE TABLE IF NOT EXISTS bbs (
    id integer primary key,
    text text,
    created timestamp default current_timestamp
);
