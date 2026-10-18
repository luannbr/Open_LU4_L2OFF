-- Current Cached.exe pledge startup batch ABI with modern donor-backed
-- transactional effects.  Classic 3.0, Ertheia and modified protocol-287
-- exports contain byte-identical bodies for both selected procedures.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_transferpledgemaster"()
RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    transfer record;
    updated_pledge_count integer;
BEGIN
    FOR transfer IN
        SELECT request.pledge_id,
               request.new_master_id,
               character.char_name AS new_master_name
        FROM "world"."pledge_master_transfer" AS request
        JOIN "world"."user_data" AS character
          ON character.char_id = request.new_master_id
         AND character.pledge_id = request.pledge_id
        WHERE character.account_id > 0
          AND request.status = 0
          AND character.pledge_type = 0
    LOOP
        UPDATE "world"."pledge" AS pledge
        SET ruler_id = transfer.new_master_id
        WHERE pledge.pledge_id = transfer.pledge_id;
        GET DIAGNOSTICS updated_pledge_count = ROW_COUNT;

        IF updated_pledge_count > 0 THEN
            UPDATE "world"."pledge_master_transfer" AS request
            SET status = 1,
                status_time = "world"."mssql_getdate"()
            WHERE request.pledge_id = transfer.pledge_id
              AND request.new_master_id = transfer.new_master_id
              AND request.status = 0;

            UPDATE "world"."sub_pledge" AS subordinate
            SET master_id = 0
            WHERE subordinate.pledge_id = transfer.pledge_id
              AND subordinate.master_id = transfer.new_master_id;
        ELSE
            UPDATE "world"."pledge_master_transfer" AS request
            SET status = 2,
                status_time = "world"."mssql_getdate"()
            WHERE request.pledge_id = transfer.pledge_id
              AND request.new_master_id = transfer.new_master_id
              AND request.status = 0;
        END IF;

        UPDATE "world"."pledge_recruit_board" AS recruitment
        SET pledge_master_name = transfer.new_master_name
        WHERE recruitment.pledge_id = transfer.pledge_id;
    END LOOP;

    UPDATE "world"."pledge_master_transfer" AS request
    SET status = -1,
        status_time = "world"."mssql_getdate"()
    WHERE request.status = 0;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_changenamebymergebatch"()
RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    changed_pledge_count integer;
BEGIN
    UPDATE "world"."pledge" AS pledge
    SET name = name_change.name_new
    FROM "world"."changed_name_by_merge" AS name_change
    WHERE pledge.pledge_id = name_change.id
      AND name_change.type = 1
      AND name_change.change_flag = 2;
    GET DIAGNOSTICS changed_pledge_count = ROW_COUNT;

    IF changed_pledge_count > 0 THEN
        UPDATE "world"."changed_name_by_merge" AS name_change
        SET change_date = "world"."mssql_getdate"(),
            change_flag = 1
        WHERE name_change.type = 1
          AND name_change.change_flag = 2;
    END IF;
END;
$function$;

COMMIT;
