-- Extended requests 231-240. Current Cached.exe fixes every callable ABI,
-- result order and Execute/Fetch branch. The Br/event-data relational bodies
-- survive in Classic 3.0/287 SQL. Reserved-announcement procedure bodies do
-- not survive locally. The two portable tables below expose only the exact
-- current five-column/two-column observable contracts.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_brsaveitemagathionenergy"(
    p_item_id integer,
    p_agathion_energy double precision
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."br_user_item_agathion_energy" AS energy
        WHERE energy.item_id = p_item_id
    ) THEN
        UPDATE "world"."br_user_item_agathion_energy" AS energy
        SET agathion_energy = p_agathion_energy
        WHERE energy.item_id = p_item_id;
    ELSE
        INSERT INTO "world"."br_user_item_agathion_energy" (
            item_id,
            agathion_energy
        ) VALUES (
            p_item_id,
            p_agathion_energy
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brloadusereventdata"(
    p_char_id integer,
    p_event_id integer
) RETURNS TABLE (event_time integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT EXTRACT(
               EPOCH FROM (
                   event_data.event_time -
                   TIMESTAMP '1970-01-01 00:00:00'
               )
           )::integer
    FROM "world"."br_user_event_data" AS event_data
    WHERE event_data.char_id = p_char_id
      AND event_data.event_id = p_event_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_brupdateusereventdata"(
    p_char_id integer,
    p_event_id integer,
    p_event_time integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM "world"."br_user_event_data" AS event_data
        WHERE event_data.char_id = p_char_id
          AND event_data.event_id = p_event_id
    ) THEN
        UPDATE "world"."br_user_event_data" AS event_data
        SET event_time =
            TIMESTAMP '1970-01-01 00:00:00' +
            make_interval(secs => p_event_time)
        WHERE event_data.char_id = p_char_id
          AND event_data.event_id = p_event_id;
    ELSE
        INSERT INTO "world"."br_user_event_data" (
            char_id,
            event_id,
            event_time
        ) VALUES (
            p_char_id,
            p_event_id,
            TIMESTAMP '1970-01-01 00:00:00' +
                make_interval(secs => p_event_time)
        );
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateuserracesex"(
    p_char_id integer,
    p_race integer,
    p_gender integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data" AS users
    SET race = p_race,
        gender = p_gender
    WHERE users.char_id = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_geteventdata"()
RETURNS TABLE (
    filename text,
    contents text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT event_data.filename::text,
           event_data.contents::text
    FROM "world"."eventdata" AS event_data
    ORDER BY event_data.create_date DESC
    LIMIT 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_saveeventdata"(
    p_filename text,
    p_comment text,
    p_contents text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."eventdata" (
        filename,
        comment,
        contents
    ) VALUES (
        p_filename,
        p_comment,
        p_contents
    );
$function$;

-- These names are portable PostgreSQL storage, not claims about the lost
-- MSSQL base-table identifiers. Their columns are limited to values exposed
-- by current lin_Get/Add/DeleteReservedAnnounce procedure ABIs.
CREATE TABLE IF NOT EXISTS "world"."reserved_announce" (
    "announce_id" integer NOT NULL,
    "start_time" timestamp without time zone NOT NULL,
    "announce_type" smallint NOT NULL,
    "repeat" integer NOT NULL,
    "flag" integer DEFAULT 0 NOT NULL,
    CONSTRAINT "pk_reserved_announce" PRIMARY KEY ("announce_id")
);

CREATE TABLE IF NOT EXISTS "world"."reserved_announce_message" (
    "announce_id" integer NOT NULL,
    "message_order" smallint NOT NULL,
    "message" text NOT NULL,
    CONSTRAINT "pk_reserved_announce_message"
        PRIMARY KEY ("announce_id", "message_order")
);

CREATE OR REPLACE FUNCTION "world"."lin_getreservedannouncelist"()
RETURNS TABLE (
    announce_id integer,
    start_time integer,
    announce_type integer,
    repeat integer,
    flag integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT announcements.announce_id,
           EXTRACT(
               EPOCH FROM (
                   announcements.start_time -
                   TIMESTAMP '1970-01-01 00:00:00'
               )
           )::integer,
           announcements.announce_type::integer,
           announcements.repeat,
           announcements.flag
    FROM "world"."reserved_announce" AS announcements
    ORDER BY announcements.announce_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getreservedannouncemsg"(
    p_announce_id integer
) RETURNS TABLE (
    message_order integer,
    message text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT messages.message_order::integer,
           messages.message
    FROM "world"."reserved_announce_message" AS messages
    WHERE messages.announce_id = p_announce_id
    ORDER BY messages.message_order;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_addreservedannounce"(
    p_announce_id integer,
    p_announce_type smallint,
    p_start_time text,
    p_repeat integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."reserved_announce" (
        announce_id,
        start_time,
        announce_type,
        repeat
    ) VALUES (
        p_announce_id,
        p_start_time::timestamp without time zone,
        p_announce_type,
        p_repeat
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletereservedannounce"(
    p_announce_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."reserved_announce" AS announcements
    WHERE announcements.announce_id = p_announce_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_addreservedannouncemsg"(
    p_announce_id integer,
    p_message_order smallint,
    p_message text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."reserved_announce_message" (
        announce_id,
        message_order,
        message
    ) VALUES (
        p_announce_id,
        p_message_order,
        p_message
    );
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletereservedannouncemsg"(
    p_announce_id integer,
    p_message_order smallint
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."reserved_announce_message" AS messages
    WHERE messages.announce_id = p_announce_id
      AND messages.message_order = p_message_order;
$function$;

COMMIT;
