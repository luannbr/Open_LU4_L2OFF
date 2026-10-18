-- Current Cached.exe RQ_EX slots 1, 2, 4, 5, 9-15 and 17-20.
--
-- The current image fixes every callable name below, its input arity and its
-- result-column order.  Classic 3.0, modified 287 and Ertheia database donors
-- agree on the mail-table effects.  The text overload of lin_deleteblocklist
-- is additionally kept by the C1 CacheD handler and SQL procedure.  The
-- current normalized user_blocklist no longer stores block_char_name, so that
-- overload resolves the supplied name through current user_data before doing
-- the same ID-keyed deletion.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_archivemail"(
    p_char_id integer,
    p_mail_id integer,
    p_mailbox_type integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_mailbox_type = 0 THEN
        UPDATE "world"."user_mail_receiver" AS receiver
           SET "mailbox_type" = 2
         WHERE receiver."mail_id" = p_mail_id
           AND receiver."receiver_id" = p_char_id;
    ELSIF p_mailbox_type = 1 THEN
        UPDATE "world"."user_mail_sender" AS sender
           SET "mailbox_type" = 2
         WHERE sender."mail_id" = p_mail_id
           AND sender."sender_id" = p_char_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletemail"(
    p_char_id integer,
    p_mail_id integer,
    p_mailbox_type integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_affected integer;
BEGIN
    UPDATE "world"."user_mail_receiver" AS receiver
       SET "deleted" = 1
     WHERE receiver."mail_id" = p_mail_id
       AND receiver."receiver_id" = p_char_id
       AND receiver."mailbox_type" = p_mailbox_type
       AND receiver."deleted" = 0;

    GET DIAGNOSTICS v_affected = ROW_COUNT;
    IF v_affected = 0 THEN
        UPDATE "world"."user_mail_sender" AS sender
           SET "deleted" = 1
         WHERE sender."mail_id" = p_mail_id
           AND sender."sender_id" = p_char_id
           AND sender."mailbox_type" = p_mailbox_type;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getblockcount"(
    p_char_id integer,
    p_block_target_id integer
) RETURNS TABLE (block_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
      FROM "world"."user_blocklist" AS block_entry
     WHERE block_entry."char_id" = p_block_target_id
       AND block_entry."block_char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getmail"(
    p_char_id integer,
    p_mail_id integer
) RETURNS TABLE (
    mail_id integer,
    sender_id integer,
    sender_name text,
    receiver_name_list text,
    title text,
    content text,
    send_date integer,
    mail_type integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_mail_receiver" AS receiver
       SET "read_date" = "world"."mssql_getdate"(),
           "read_status" = 1
     WHERE receiver."mail_id" = p_mail_id
       AND receiver."receiver_id" = p_char_id
       AND receiver."read_status" = 0
       AND receiver."deleted" = 0;

    RETURN QUERY
    SELECT mail."id",
           sender."sender_id",
           sender."sender_name"::text,
           sender."receiver_name_list"::text,
           mail."title"::text,
           mail."content"::text,
           extract(epoch FROM sender."send_date")::integer,
           sender."mail_type"::integer
      FROM "world"."user_mail" AS mail
      JOIN "world"."user_mail_sender" AS sender
        ON sender."mail_id" = mail."id"
     WHERE mail."id" = p_mail_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getmailcount"(
    p_char_id integer
) RETURNS TABLE (
    incoming_mail integer,
    sent_mail integer,
    archived_mail integer,
    temporary_mail integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT
        (SELECT count(*)::integer
           FROM "world"."user_mail_receiver" AS receiver
          WHERE receiver."receiver_id" = p_char_id
            AND receiver."deleted" = 0
            AND receiver."mailbox_type" = 0),
        (SELECT count(*)::integer
           FROM "world"."user_mail_sender" AS sender
          WHERE sender."sender_id" = p_char_id
            AND sender."deleted" = 0
            AND sender."mailbox_type" = 1),
        ((SELECT count(*)
            FROM "world"."user_mail_receiver" AS receiver
           WHERE receiver."receiver_id" = p_char_id
             AND receiver."deleted" = 0
             AND receiver."mailbox_type" = 2)
         +
         (SELECT count(*)
            FROM "world"."user_mail_sender" AS sender
           WHERE sender."sender_id" = p_char_id
             AND sender."deleted" = 0
             AND sender."mailbox_type" = 2))::integer,
        (SELECT count(*)::integer
           FROM "world"."user_mail_sender" AS sender
          WHERE sender."sender_id" = p_char_id
            AND sender."deleted" = 0
            AND sender."mailbox_type" = 3);
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getmaillist"(
    p_char_id integer,
    p_mailbox_type integer,
    p_page integer,
    p_rows_per_page integer,
    p_search_target integer,
    p_keyword text
) RETURNS TABLE (
    mail_id integer,
    sender_id integer,
    display_name text,
    title text,
    send_date integer,
    type_flag integer,
    total_count integer
)
LANGUAGE plpgsql
STABLE
AS $function$
DECLARE
    v_page integer := CASE WHEN p_page < 1 THEN 1 ELSE p_page END;
    v_top integer;
    v_keyword text := COALESCE(p_keyword, '');
BEGIN
    v_top := v_page * p_rows_per_page;
    IF v_top < 1 THEN
        RETURN;
    END IF;

    IF p_mailbox_type = 0 THEN
        RETURN QUERY
        SELECT rows."mail_id",
               rows."sender_id",
               rows."display_name",
               rows."title",
               rows."send_date",
               rows."type_flag",
               count(*) OVER ()::integer
          FROM (
              SELECT mail."id" AS mail_id,
                     sender."sender_id" AS sender_id,
                     sender."sender_name"::text AS display_name,
                     mail."title"::text AS title,
                     extract(epoch FROM sender."send_date")::integer
                         AS send_date,
                     receiver."read_status"::integer AS type_flag
                FROM "world"."user_mail" AS mail
                JOIN "world"."user_mail_receiver" AS receiver
                  ON receiver."mail_id" = mail."id"
                JOIN "world"."user_mail_sender" AS sender
                  ON sender."mail_id" = receiver."mail_id"
               WHERE receiver."receiver_id" = p_char_id
                 AND receiver."deleted" = 0
                 AND receiver."mailbox_type" = p_mailbox_type
                 AND (
                     "world"."mssql_text_eq"(v_keyword, '')
                     OR p_search_target NOT IN (0, 1)
                     OR (
                         p_search_target = 0
                         AND EXISTS (
                             SELECT 1
                               FROM "world"."user_mail_sender" AS searched
                              WHERE searched."mail_id" = receiver."mail_id"
                                AND "world"."mssql_text_eq"(
                                    searched."sender_name",
                                    v_keyword
                                )
                         )
                     )
                     OR (
                         p_search_target = 1
                         AND mail."title" LIKE '%' || v_keyword || '%'
                     )
                 )
          ) AS rows
         ORDER BY rows."send_date" DESC
         LIMIT v_top;
    ELSIF p_mailbox_type = 1 THEN
        RETURN QUERY
        SELECT rows."mail_id",
               rows."sender_id",
               rows."display_name",
               rows."title",
               rows."send_date",
               rows."type_flag",
               count(*) OVER ()::integer
          FROM (
              SELECT mail."id" AS mail_id,
                     sender."sender_id" AS sender_id,
                     sender."receiver_name_list"::text AS display_name,
                     mail."title"::text AS title,
                     extract(epoch FROM sender."send_date")::integer
                         AS send_date,
                     sender."mail_type"::integer AS type_flag
                FROM "world"."user_mail" AS mail
                JOIN "world"."user_mail_sender" AS sender
                  ON sender."mail_id" = mail."id"
               WHERE sender."sender_id" = p_char_id
                 AND sender."deleted" = 0
                 AND sender."mailbox_type" = p_mailbox_type
                 AND (
                     "world"."mssql_text_eq"(v_keyword, '')
                     OR p_search_target NOT IN (0, 1)
                     OR (
                         p_search_target = 0
                         AND EXISTS (
                             SELECT 1
                               FROM "world"."user_mail_receiver" AS searched
                              WHERE searched."mail_id" = sender."mail_id"
                                AND "world"."mssql_text_eq"(
                                    searched."receiver_name",
                                    v_keyword
                                )
                         )
                     )
                     OR (
                         p_search_target = 1
                         AND mail."title" LIKE '%' || v_keyword || '%'
                     )
                 )
          ) AS rows
         ORDER BY rows."send_date" DESC
         LIMIT v_top;
    ELSIF p_mailbox_type = 2 THEN
        RETURN QUERY
        SELECT rows."mail_id",
               rows."sender_id",
               rows."display_name",
               rows."title",
               rows."send_date",
               rows."type_flag",
               count(*) OVER ()::integer
          FROM (
              SELECT mail."id" AS mail_id,
                     sender."sender_id" AS sender_id,
                     sender."sender_name"::text AS display_name,
                     mail."title"::text AS title,
                     extract(epoch FROM sender."send_date")::integer
                         AS send_date,
                     receiver."read_status"::integer AS type_flag
                FROM "world"."user_mail" AS mail
                JOIN "world"."user_mail_receiver" AS receiver
                  ON receiver."mail_id" = mail."id"
                JOIN "world"."user_mail_sender" AS sender
                  ON sender."mail_id" = receiver."mail_id"
               WHERE receiver."receiver_id" = p_char_id
                 AND receiver."deleted" = 0
                 AND receiver."mailbox_type" = p_mailbox_type
                 AND (
                     "world"."mssql_text_eq"(v_keyword, '')
                     OR p_search_target NOT IN (0, 1)
                     OR (
                         p_search_target = 0
                         AND "world"."mssql_text_eq"(
                             sender."sender_name",
                             v_keyword
                         )
                     )
                     OR (
                         p_search_target = 1
                         AND mail."title" LIKE '%' || v_keyword || '%'
                     )
                 )
              UNION ALL
              SELECT mail."id" AS mail_id,
                     sender."sender_id" AS sender_id,
                     sender."receiver_name_list"::text AS display_name,
                     mail."title"::text AS title,
                     extract(epoch FROM sender."send_date")::integer
                         AS send_date,
                     sender."mail_type"::integer AS type_flag
                FROM "world"."user_mail" AS mail
                JOIN "world"."user_mail_sender" AS sender
                  ON sender."mail_id" = mail."id"
               WHERE sender."sender_id" = p_char_id
                 AND sender."deleted" = 0
                 AND sender."mailbox_type" = p_mailbox_type
                 AND (
                     "world"."mssql_text_eq"(v_keyword, '')
                     OR p_search_target NOT IN (0, 1)
                     OR (
                         p_search_target = 0
                         AND EXISTS (
                             SELECT 1
                               FROM "world"."user_mail_receiver" AS searched
                              WHERE searched."mail_id" = sender."mail_id"
                                AND "world"."mssql_text_eq"(
                                    searched."receiver_name",
                                    v_keyword
                                )
                         )
                     )
                     OR (
                         p_search_target = 1
                         AND mail."title" LIKE '%' || v_keyword || '%'
                     )
                 )
          ) AS rows
         ORDER BY rows."send_date" DESC
         LIMIT v_top;
    ELSIF p_mailbox_type = 3 THEN
        RETURN QUERY
        SELECT rows."mail_id",
               rows."sender_id",
               rows."display_name",
               rows."title",
               rows."send_date",
               rows."type_flag",
               count(*) OVER ()::integer
          FROM (
              SELECT mail."id" AS mail_id,
                     sender."sender_id" AS sender_id,
                     sender."receiver_name_list"::text AS display_name,
                     mail."title"::text AS title,
                     extract(epoch FROM sender."send_date")::integer
                         AS send_date,
                     sender."mail_type"::integer AS type_flag
                FROM "world"."user_mail" AS mail
                JOIN "world"."user_mail_sender" AS sender
                  ON sender."mail_id" = mail."id"
               WHERE sender."sender_id" = p_char_id
                 AND sender."deleted" = 0
                 AND sender."mailbox_type" = p_mailbox_type
                 AND (
                     "world"."mssql_text_eq"(v_keyword, '')
                     OR p_search_target NOT IN (0, 1)
                     OR (
                         p_search_target = 0
                         AND sender."receiver_name_list"
                             LIKE '%' || v_keyword || '%'
                     )
                     OR (
                         p_search_target = 1
                         AND mail."title" LIKE '%' || v_keyword || '%'
                     )
                 )
          ) AS rows
         ORDER BY rows."send_date" DESC
         LIMIT v_top;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getsendmailcount"(
    p_char_id integer
) RETURNS TABLE (send_mail_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
      FROM "world"."user_mail_sender" AS sender
     WHERE sender."sender_id" IN (
               SELECT account_character."char_id"
                 FROM "world"."user_data" AS account_character
                WHERE account_character."account_id" = (
                    SELECT owner."account_id"
                      FROM "world"."user_data" AS owner
                     WHERE owner."char_id" = p_char_id
                )
           )
       AND sender."mailbox_type" = 1
       AND sender."send_date" >= "world"."mssql_getdate"()::date
       AND sender."send_date" <
           "world"."mssql_getdate"()::date + INTERVAL '1 day';
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_gettempmail"(
    p_char_id integer,
    p_mail_id integer
) RETURNS TABLE (
    mail_id integer,
    receiver_name_list text,
    title text,
    content text
)
LANGUAGE sql
STABLE
AS $function$
    SELECT mail."id",
           sender."receiver_name_list"::text,
           mail."title"::text,
           mail."content"::text
     FROM "world"."user_mail" AS mail
      JOIN "world"."user_mail_sender" AS sender
        ON sender."mail_id" = mail."id"
     WHERE mail."id" = p_mail_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getunreadmailcount"(
    p_char_id integer
) RETURNS TABLE (unread_mail_count integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT count(*)::integer
      FROM "world"."user_mail_receiver" AS receiver
     WHERE receiver."receiver_id" = p_char_id
       AND receiver."mailbox_type" = 0
       AND receiver."read_status" = 0
       AND receiver."deleted" = 0;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_modifytempmail"(
    p_char_id integer,
    p_mail_id integer,
    p_receiver_name_list text,
    p_title text,
    p_content text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_mail" AS mail
       SET "title" = p_title,
           "content" = p_content,
           "created_date" = "world"."mssql_getdate"()
     WHERE mail."id" = p_mail_id;

    UPDATE "world"."user_mail_sender" AS sender
       SET "receiver_name_list" = p_receiver_name_list,
           "send_date" = "world"."mssql_getdate"()
     WHERE sender."mail_id" = p_mail_id
       AND sender."sender_id" = p_char_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_modifysendtempmail"(
    p_char_id integer,
    p_mail_id integer,
    p_receiver_name_list text,
    p_title text,
    p_content text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_mail_type integer;
    v_receiver record;
BEGIN
    UPDATE "world"."user_mail" AS mail
       SET "title" = p_title,
           "content" = p_content,
           "created_date" = "world"."mssql_getdate"()
     WHERE mail."id" = p_mail_id;

    UPDATE "world"."user_mail_sender" AS sender
       SET "receiver_name_list" = p_receiver_name_list,
           "send_date" = "world"."mssql_getdate"(),
           "mailbox_type" = 1
     WHERE sender."mail_id" = p_mail_id
       AND sender."sender_id" = p_char_id;

    SELECT sender."mail_type"::integer
      INTO v_mail_type
      FROM "world"."user_mail_sender" AS sender
     WHERE sender."mail_id" = p_mail_id;

    IF v_mail_type = 1 THEN
        FOR v_receiver IN
            SELECT member."char_id", member."char_name"
              FROM "world"."user_data" AS member
              JOIN "world"."pledge" AS pledge
                ON pledge."pledge_id" = member."pledge_id"
             WHERE "world"."mssql_text_eq"(
                 pledge."name",
                 p_receiver_name_list
             )
        LOOP
            INSERT INTO "world"."user_mail_receiver" (
                "mail_id",
                "receiver_id",
                "receiver_name"
            ) VALUES (
                p_mail_id,
                v_receiver."char_id",
                v_receiver."char_name"
            );
        END LOOP;
    ELSE
        FOR v_receiver IN
            SELECT receiver."char_id", receiver."char_name"
              FROM regexp_split_to_table(
                       COALESCE(p_receiver_name_list, ''),
                       ';'
                   ) AS supplied("char_name")
              JOIN "world"."user_data" AS receiver
                ON "world"."mssql_text_eq"(
                    receiver."char_name",
                    supplied."char_name"
                )
             WHERE "world"."mssql_text_ne"(supplied."char_name", '')
        LOOP
            INSERT INTO "world"."user_mail_receiver" (
                "mail_id",
                "receiver_id",
                "receiver_name"
            ) VALUES (
                p_mail_id,
                v_receiver."char_id",
                v_receiver."char_name"
            );
        END LOOP;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savemail"(
    p_char_id integer,
    p_mail_type integer,
    p_related_id integer,
    p_receiver_name_list text,
    p_title text,
    p_content text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_mail_id integer;
    v_sender_name text;
BEGIN
    INSERT INTO "world"."user_mail" ("title", "content")
    VALUES (p_title, p_content)
    RETURNING "id" INTO v_mail_id;

    SELECT owner."char_name"
      INTO v_sender_name
      FROM "world"."user_data" AS owner
     WHERE owner."char_id" = p_char_id;

    INSERT INTO "world"."user_mail_sender" (
        "mail_id",
        "related_id",
        "mail_type",
        "mailbox_type",
        "sender_id",
        "sender_name",
        "receiver_name_list"
    ) VALUES (
        v_mail_id,
        p_related_id,
        p_mail_type,
        3,
        p_char_id,
        v_sender_name,
        p_receiver_name_list
    );
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_sendmail"(
    p_char_id integer,
    p_mail_type integer,
    p_related_id integer,
    p_receiver_name_list text,
    p_title text,
    p_content text
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_mail_id integer;
    v_sender_name text;
    v_receiver record;
BEGIN
    INSERT INTO "world"."user_mail" ("title", "content")
    VALUES (p_title, p_content)
    RETURNING "id" INTO v_mail_id;

    SELECT owner."char_name"
      INTO v_sender_name
      FROM "world"."user_data" AS owner
     WHERE owner."char_id" = p_char_id;

    INSERT INTO "world"."user_mail_sender" (
        "mail_id",
        "related_id",
        "mail_type",
        "sender_id",
        "sender_name",
        "receiver_name_list"
    ) VALUES (
        v_mail_id,
        p_related_id,
        p_mail_type,
        p_char_id,
        v_sender_name,
        p_receiver_name_list
    );

    IF p_mail_type = 1 THEN
        FOR v_receiver IN
            SELECT member."char_id", member."char_name"
              FROM "world"."user_data" AS member
              JOIN "world"."pledge" AS pledge
                ON pledge."pledge_id" = member."pledge_id"
             WHERE "world"."mssql_text_eq"(
                 pledge."name",
                 p_receiver_name_list
             )
        LOOP
            INSERT INTO "world"."user_mail_receiver" (
                "mail_id",
                "receiver_id",
                "receiver_name"
            ) VALUES (
                v_mail_id,
                v_receiver."char_id",
                v_receiver."char_name"
            );
        END LOOP;
    ELSE
        FOR v_receiver IN
            SELECT receiver."char_id", receiver."char_name"
              FROM regexp_split_to_table(
                       COALESCE(p_receiver_name_list, ''),
                       ';'
                   ) AS supplied("char_name")
              JOIN "world"."user_data" AS receiver
                ON "world"."mssql_text_eq"(
                    receiver."char_name",
                    supplied."char_name"
                )
             WHERE "world"."mssql_text_ne"(supplied."char_name", '')
        LOOP
            INSERT INTO "world"."user_mail_receiver" (
                "mail_id",
                "receiver_id",
                "receiver_name"
            ) VALUES (
                v_mail_id,
                v_receiver."char_id",
                v_receiver."char_name"
            );
        END LOOP;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deleteblocklist"(
    p_char_id integer,
    p_block_char_name text
) RETURNS TABLE (block_char_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_block_char_id integer;
    v_affected integer;
BEGIN
    SELECT blocked."char_id"
      INTO v_block_char_id
      FROM "world"."user_data" AS blocked
     WHERE "world"."mssql_text_eq"(blocked."char_name", p_block_char_name);

    -- DBConn sends libpq parameters without PostgreSQL type OIDs.  With both
    -- current Cached call shapes installed, PostgreSQL therefore selects this
    -- text overload even for CBlockCharDB::DeleteBlockChar's integer target
    -- ID.  Preserve the HTML name path above and accept the current FriendsDb
    -- ID path only when no character has that exact name.
    IF v_block_char_id IS NULL
       AND p_block_char_name ~ '^[0-9]+$' THEN
        SELECT blocked."char_id"
          INTO v_block_char_id
          FROM "world"."user_data" AS blocked
         WHERE blocked."char_id" = p_block_char_name::integer;
    END IF;

    IF v_block_char_id IS NULL THEN
        RAISE EXCEPTION 'query returned no rows' USING ERRCODE = 'P0002';
    END IF;

    DELETE FROM "world"."user_blocklist" AS block_entry
     WHERE block_entry."char_id" = p_char_id
       AND block_entry."block_char_id" = v_block_char_id;

    GET DIAGNOSTICS v_affected = ROW_COUNT;
    IF v_affected <> 1 THEN
        RAISE EXCEPTION 'block target is not present';
    END IF;

    RETURN QUERY SELECT v_block_char_id;
END;
$function$;

COMMIT;
